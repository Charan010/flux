#include "shared_writer.h"

#include "compression_job.h"
#include "huffman.h"

SharedWriter::SharedWriter()
    : writer_thread(&SharedWriter::writer_loop, this) {}

SharedWriter::~SharedWriter() {

  {
    std::lock_guard lock(mtx);
    running = false;
  }

  cv.notify_one();

  if (writer_thread.joinable())
    writer_thread.join();
}

void SharedWriter::register_job(std::shared_ptr<WriterJob> job) {

  std::lock_guard lock(mtx);
  jobs[job->id] = std::move(job);
  cv.notify_one();
}

void SharedWriter::notify() { cv.notify_one(); }

void SharedWriter::writer_loop() {

  while (true) {

    std::unique_lock lock(mtx);
    cv.wait(lock, [this] { return !running || !jobs.empty(); });

    if (!running && jobs.empty())
      break;

    bool made_progress = true;

    while (made_progress) {

      made_progress = false;

      for (auto it = jobs.begin(); it != jobs.end();) {

        auto &job = it->second;

        // Drain all consecutive in-order chunks available right now
        while (true) {

          Chunk chunk;
          if (!job->queue->try_pop(chunk))
            break;

          made_progress = true;

          lock.unlock();

          if (job->raw_output) {
            job->writer->write_bytes(chunk.data);
          } else {
            write_uint32(*job->writer, chunk.compressed_bytes);
            write_uint32(*job->writer, chunk.bit_count);
            job->writer->write_bytes(chunk.data);
          }

          lock.lock();
        }

        if (job->queue->is_done()) {

          // Final drain after queue is closed
          bool final_drained = false;

          while (true) {
            Chunk chunk;
            if (!job->queue->try_pop(chunk))
              break;

            final_drained = true;

            lock.unlock();

            if (job->raw_output) {
              job->writer->write_bytes(chunk.data);
            } else {
              write_uint32(*job->writer, chunk.compressed_bytes);
              write_uint32(*job->writer, chunk.bit_count);
              job->writer->write_bytes(chunk.data);
            }

            lock.lock();
          }

          if (final_drained)
            made_progress = true;

          lock.unlock();

          job->writer->flush();

          if (job->on_complete)
            job->on_complete();

          lock.lock();

          it = jobs.erase(it);
          made_progress = true;

        } else {
          ++it;
        }
      }
    }
  }
}
