#include "shared_writer.h"

SharedWriter::SharedWriter():
writer_thread(&SharedWriter::writer_loop, this) {}

SharedWriter::~SharedWriter(){
    {
        std::lock_guard<std::mutex> lock(mtx);
        running = false;
    }

    cv.notify_all();

    if(writer_thread.joinable())
        writer_thread.join();
}


void SharedWriter::register_job(std::shared_ptr<WriterJob> job){

    std::lock_guard lock(mtx);
    jobs[job -> id] = std::move(job); 

}

void SharedWriter::notify_ready(int job_id) {

    {
        std::lock_guard lock(mtx);
        ready_jobs.push(job_id);
    }

    cv.notify_one();
}

void SharedWriter::writer_loop(){

    while(true){

        int job_id;

        {
            std::unique_lock lock(mtx);

            /* Sleep until the writer thread is stopped or there are no ready jobs. */
            cv.wait(lock, [&]{
                return !running || !ready_jobs.empty(); 
            });

            if(!running && ready_jobs.empty())
                return;

            job_id = ready_jobs.front();
            ready_jobs.pop();
        }

        std::shared_ptr<WriterJob> job;

        {
            std::lock_guard lock(mtx);

            auto it = jobs.find(job_id);

            if(it == jobs.end())
                continue;

            job = it -> second;
        }

        /*
        * Drains all contigous ready chunks in one go.
        * The main issue is that suppose chunk x+1 has arrived before chunk x. So, when the writer thread
        * gets notified for chunk x and writes chunk x to the file. OrderedQueue doesnt notify about chunk x + 1 arrival.
        * This can cause deadlock as all chunks are just waiting in memory and writer is not draining them.
        */

        while(true){

            auto chunk_opt = job -> queue -> try_pop();

            if(!chunk_opt)
                break;

            job -> writer -> write_bytes(chunk_opt -> data);
        }

        job->queue->clear_scheduled();

         if (auto chunk = job->queue->try_pop()) {

            job->writer->write_bytes(chunk->data);

            notify_ready(job_id);
        }

        if (job->queue->is_done()) {

            std::lock_guard lock(mtx);

            jobs.erase(job_id);
        }

    }
}

