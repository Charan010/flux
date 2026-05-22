#include "compression_job.h"

#include <algorithm>
#include <memory>
#include <stdexcept>

#include "bit_io.h"
#include "chunk.h"
#include "huffman_engine.h"
#include "mmap_file.h"

CompressionJob::CompressionJob(uint64_t job_id, Threadpool &pool, SharedWriter & shared_writer, 
CompressionMode mode, const std::string &input_file, const std::string &output_file, size_t chunk_size)

: job_id_(job_id), pool_(pool), shared_writer_(shared_writer), mode_(mode), input_file_(input_file),
output_file_(output_file), chunk_size_(chunk_size) {}


uint64_t CompressionJob::id() const{
    return job_id_;
}

JobState CompressionJob::state() const{
    return state_.load();
}

const std::string& CompressionJob::input_path() const {
    return input_file_;
}

const std::string& CompressionJob::output_path() const {
    return output_file_;
}

std::shared_ptr<CodecEngine> CompressionJob::create_engine(){

    switch(mode_){

        case CompressionMode::Huffman:
            return std::make_shared<HuffmanEngine>();

        default:
            throw std::runtime_error("unsupported codec");
    }

}

void CompressionJob::start_async() {

    state_ = JobState::RUNNING;
    auto self = shared_from_this();

    pool_.submit([self] {

        try {

            /*
            * Keep mmap alive for all worker tasks.
            */
            auto input = std::make_shared<MappedFile>(self->input_file_);

            if (input->size() == 0)
                throw std::runtime_error("input file is empty");

            auto engine = self->create_engine();

            engine->prepare_encoder(input->data(), input->size());
            const size_t total_chunks = (input->size() + self->chunk_size_ - 1) / self->chunk_size_;
            self->remaining_chunks_ = total_chunks;

            /*
            * Shared ordered output buffer.
            */
            self->ordered_queue_ =std::make_shared<OrderedQueue>(total_chunks, &self->shared_writer_, self->job_id_);

            /*
            * Writer job registration.
            */

            auto writer_job = std::make_shared<WriterJob>();

            writer_job->id = self->job_id_;
            writer_job->queue = self->ordered_queue_;
            writer_job->writer = std::make_shared<BitWriter>(self->output_file_);





            writer_job->owner = self;

            /*
            * Keep local ownership too.
            */
            self->writer_job_ = writer_job;
            self->shared_writer_.register_job(writer_job);

            engine->write_global_header(*writer_job->writer, static_cast<uint32_t>(input->size()),
                static_cast<uint32_t>(total_chunks));

            /*
            * Submit compression workers.
            */
            for (size_t i = 0; i < total_chunks; ++i) {

                const size_t offset = i * self->chunk_size_;
                const size_t len = std::min(self->chunk_size_, input->size() - offset);

                self->pool_.submit([self, engine, input, offset, len, i] {

                    try {

                        Chunk chunk;
                        chunk.id = static_cast<uint32_t>(i);

                        engine->encode_chunk(input->data() + offset, len, chunk);

                        self->ordered_queue_->push(std::move(chunk));

                        /*
                        * Last producer closes queue.
                        */
                        if (self->remaining_chunks_.fetch_sub(1) == 1) {

                            self->state_ = JobState::WRITING;
                            self->ordered_queue_->close();
                        }

                    } catch (...) {
                        self->mark_failed();
                    }
                });
            }

        } catch (...) {
            self->mark_failed();
        }
    });

}

