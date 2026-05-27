#include "compression_job.h"

#include <algorithm>
#include <memory>
#include <stdexcept>

#include "bit_io.h"
#include "chunk.h"
#include "huffman_engine.h"
#include "mmap_file.h"
#include <LZ4_engine.h>

CompressionJob::CompressionJob(uint64_t job_id, Threadpool &pool, SharedWriter &shared_writer, CompressionMode mode,
const std::string &input_file, const std::string &output_file, size_t chunk_size)

    : job_id(job_id), pool(pool), shared_writer(shared_writer), mode(mode), input_file(input_file),
	output_file(output_file), chunk_size(chunk_size)
	
	{}

uint64_t CompressionJob::id() const{
	 return job_id; 
}

JobState CompressionJob::state()const{
	 return state_.load(); 
}

const std::string &CompressionJob::input_path()const{
	 return input_file; 
}

const std::string &CompressionJob::output_path()const{
	 return output_file; 
}

std::shared_ptr<CodecEngine> CompressionJob::create_engine(){

	switch (mode) {

  		case CompressionMode::Huffman:
    		return std::make_shared<HuffmanEngine>();

  		case CompressionMode::LZ4:
    		return std::make_shared<LZ4Engine>();

  		default:
    		throw std::runtime_error("unsupported codec");
  	}
}

void CompressionJob::dispatch() {

	state_ = JobState::RUNNING;
  	auto self = shared_from_this();

  	pool.submit([self] {
    	try {

      		auto input = std::make_shared<MappedFile>(self->input_file);

      		if (input->size() == 0)
        		throw std::runtime_error("input file is empty");

      		auto engine = self->create_engine();
      		engine->prepare_encoder(input->data(), input->size());

      		const size_t total_chunks = (input->size() + self->chunk_size - 1) / self->chunk_size;
      		self->remaining_chunks = total_chunks;

      		self->ordered_queue = std::make_shared<OrderedQueue>(total_chunks, &self->shared_writer, self->job_id);
      		auto writer_job = std::make_shared<WriterJob>();

      		writer_job->id = self->job_id;
      		writer_job->queue = self->ordered_queue;
      		writer_job->writer = std::make_shared<BitWriter>(self->output_file);

      		writer_job->on_complete = [self] {self->mark_completed();
        		if (self->on_complete)
          			self->on_complete(true);
      		};

			
      		engine->write_global_header(*writer_job->writer, static_cast<uint32_t>(input->size()), static_cast<uint32_t>(total_chunks), 
			static_cast<uint32_t>(self->chunk_size));

      		self->writer_job = writer_job;
      		self->shared_writer.register_job(writer_job);

      		for (size_t i = 0; i < total_chunks; ++i) {

        		const size_t offset = i * self->chunk_size;
        		const size_t len = std::min(self->chunk_size, input->size() - offset);

		        self->pool.submit([self, engine, input, offset, len, i] {

          		try {

            		Chunk chunk;
            		chunk.id = static_cast<uint32_t>(i);

            		engine->encode_chunk(input->data() + offset, len, chunk);
            		self->ordered_queue->push(std::move(chunk));


					/* If only one chunk remaining to pass on to threadpool then transition to WRITING phase. */
            		if (self->remaining_chunks.fetch_sub(1) == 1) {
              			self->state_ = JobState::WRITING;
              			self->ordered_queue->close();
            		}


        		}catch (const std::exception &e) {
            		self->mark_failed(e.what());
          		}


			catch (...){self->mark_failed("unknown error in chunk worker"); }

        	});

      	}

    	}catch (const std::exception &e){ self->mark_failed(e.what()); }
		catch (...) { self->mark_failed("unknown error during dispatch"); }


	});
}

void CompressionJob::mark_completed(){
	 state_ = JobState::COMPLETED; 
}

void CompressionJob::mark_failed(){
	 mark_failed("unknown error"); 
}

void CompressionJob::mark_failed(std::string reason) {

  std::lock_guard lock(error_mtx);
  	if (!last_error_.empty())
    	return;

  	last_error_ = std::move(reason);
  	state_ = JobState::FAILED;

  	if (ordered_queue)
		ordered_queue->close();

	if (on_complete)
    	on_complete(false);
}
