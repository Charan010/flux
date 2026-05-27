#include "decompression_job.h"

#include <algorithm>
#include <iostream>
#include <memory>
#include <stdexcept>

#include "bit_io.h"
#include "chunk.h"
#include "huffman_engine.h"
#include "mmap_file.h"


static constexpr size_t GLOBAL_HEADER_BYTES = 4 + 1 + 256 + 4 + 4 + 4;

static inline uint32_t read_be32(const uint8_t *p, size_t pos) {
	return (static_cast<uint32_t>(p[pos]) << 24) | (static_cast<uint32_t>(p[pos + 1]) << 16) |
         (static_cast<uint32_t>(p[pos + 2]) << 8) | static_cast<uint32_t>(p[pos + 3]);
}


DecompressionJob::DecompressionJob(uint64_t job_id, Threadpool &pool, SharedWriter &shared_writer, CompressionMode mode, 
const std::string &input_file, const std::string &output_file, size_t chunk_size)

    : job_id_(job_id), pool_(pool), shared_writer_(shared_writer), mode_(mode), input_file_(input_file),
	  output_file_(output_file), chunk_size_(chunk_size)
{}


uint64_t DecompressionJob::id()const{
	return job_id_; 
}

JobState DecompressionJob::state()const{
	 return state_.load(); 
}

const std::string &DecompressionJob::input_path()const{
	 return input_file_; 
}

const std::string &DecompressionJob::output_path()const {
  	return output_file_;
}

std::shared_ptr<CodecEngine> DecompressionJob::create_engine(){
  	switch (mode_) {
  		case CompressionMode::Huffman:
    		return std::make_shared<HuffmanEngine>();
  		case CompressionMode::LZ4:
    		return std::make_shared<LZ4Engine>();
  		default:
    		throw std::runtime_error("unsupported codec");
  	}

}

void DecompressionJob::dispatch() {
  	state_ = JobState::RUNNING;
  	auto self = shared_from_this();

  	pool_.submit([self] {
    	try {

      	auto input = std::make_shared<MappedFile>(self->input_file_);

      	static constexpr size_t MIN_HEADER_BYTES = 4 + 1 + 4 + 4 + 4; 
      	if (input->size() < MIN_HEADER_BYTES)
        	throw std::runtime_error("compressed file too small");

      	BitReader br(input->data(), input->size());

      	for (uint8_t expected : Config::MAGIC) {
        	if (br.read_byte() != expected)
          		throw std::runtime_error("bad magic");
      	}

      	uint8_t codec = br.read_byte();
      		switch (codec) {
      			case 0:
        			self->mode_ = CompressionMode::Huffman;
        			break;
      			case 1:
        			self->mode_ = CompressionMode::LZ4;
        			break;
      			default:
        			throw std::runtime_error("unknown codec");
      		}
			
      	auto engine = self->create_engine();

      	uint32_t orig_size = 0;
      	uint32_t num_chunks = 0;
      	uint32_t header_chunk_size = 0; 

      	engine->read_global_header(input->data(), input->size(), orig_size, num_chunks, header_chunk_size);

      	if (num_chunks == 0)
        	throw std::runtime_error("zero chunks");

      	self->chunk_size_ = header_chunk_size;
      	self->remaining_chunks_ = num_chunks;

      	self->ordered_queue_ = std::make_shared<OrderedQueue>(num_chunks, &self->shared_writer_, self->job_id_);

      	auto writer_job = std::make_shared<WriterJob>();
      	writer_job->id = self->job_id_;
      	writer_job->queue = self->ordered_queue_;
      	writer_job->writer = std::make_shared<BitWriter>(self->output_file_);
      	writer_job->raw_output = true;

      	writer_job->on_complete = [self] {
			self->mark_completed();
        	if (self->on_complete_)
          	self->on_complete_(true);
      	};

      	self->writer_job_ = writer_job;
      	self->shared_writer_.register_job(writer_job);

      	size_t pos = GLOBAL_HEADER_BYTES;

      	if (self->mode_ == CompressionMode::LZ4)
        	pos = 4 + 1 + 4 + 4 + 4; // 17

      	for (uint32_t i = 0; i < num_chunks; ++i) {

        	if (pos + 8 > input->size())
          	throw std::runtime_error("truncated chunk header");

        	uint32_t compressed_bytes = read_be32(input->data(), pos);
        	pos += 4;
        	uint32_t bit_count = read_be32(input->data(), pos);
        	pos += 4;

        	if (compressed_bytes == 0)
          		throw std::runtime_error("compressed_bytes=0");

        	if (self->mode_ == CompressionMode::Huffman) {
          		if (bit_count == 0)
            		throw std::runtime_error("bit_count=0");

          		if (bit_count > static_cast<uint64_t>(compressed_bytes) * 8)
            		throw std::runtime_error("invalid bit_count");
        	}

        	if (pos + compressed_bytes > input->size())
          		throw std::runtime_error("truncated payload");

        	uint32_t original_size;
        	if (i + 1 < num_chunks)
          		original_size = static_cast<uint32_t>(self->chunk_size_);
        	else {
          		uint64_t already = static_cast<uint64_t>(self->chunk_size_) * (num_chunks - 1);
          		if (already > orig_size)
            		throw std::runtime_error("corrupt metadata");
          		uint64_t remaining = orig_size - already;
          		original_size = (remaining == 0) ? static_cast<uint32_t>(self->chunk_size_) : 
					static_cast<uint32_t>(remaining);
        }


        const uint8_t *payload_ptr = input->data() + pos;
        pos += compressed_bytes;


        self->pool_.submit([self, engine, input, payload_ptr, compressed_bytes, bit_count, original_size, i]() mutable{
        	
			try {
            	Chunk chunk;
            	chunk.id = i;
            	chunk.original_size = original_size;
            	chunk.bit_count = bit_count;
            	chunk.compressed_bytes = compressed_bytes;

            	engine->decode_chunk(payload_ptr, compressed_bytes, chunk);

            	if (chunk.data.size() != original_size)
              		throw std::runtime_error("decoded size mismatch");

            	self->ordered_queue_->push(std::move(chunk));

            	if (self->remaining_chunks_.fetch_sub(1) == 1) {
              	self->state_ = JobState::WRITING;
              	self->ordered_queue_->close();
            	}
          	}catch(const std::exception &e){

            	self->mark_failed(e.what());
            	self->ordered_queue_->close();

          	}catch (...) {
            	self->mark_failed("unknown error in chunk worker");
            	self->ordered_queue_->close();
          }

        });

      }

    }catch (const std::exception &e) {
      	self->mark_failed(e.what());
      	if (self->ordered_queue_)
        	self->ordered_queue_->close();
    	}catch (...){

      	self->mark_failed("unknown error during dispatch");
      		if (self->ordered_queue_)
				self->ordered_queue_->close();
    	}
  });
}

void DecompressionJob::mark_completed(){
	state_ = JobState::COMPLETED; 
}

void DecompressionJob::mark_failed(){
	 mark_failed("unknown error"); 
}

void DecompressionJob::mark_failed(std::string reason){

  std::call_once(fail_once_, [this, &reason] {
    {

      std::lock_guard lock(error_mtx_);
      last_error_ = std::move(reason);

    }

    state_ = JobState::FAILED;
    if (ordered_queue_)
      ordered_queue_->close();
	  
  });
}