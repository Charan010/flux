#pragma once

#include <condition_variable>
#include <memory>
#include <mutex>
#include <queue>
#include <thread>
#include <unordered_map>

#include "bit_io.h"
#include "ordered_queue.h"

/*
* SharedWriter:
* Centralized serial writer for all compression and decompression jobs.
*
*

* Contract:
* -Multiple jobs can may enqueue write notifications concurrently.
* -Single internal thread performs writing of chunks to disk.
* - Each OrderedQueue gurantess in-order chunk delivery.

*/



struct WriterJob{

    int id;
    std::shared_ptr<OrderedQueue> queue;
    std::shared_ptr<BitWriter> writer;
    std::weak_ptr<CompressionJob> owner;

};


class SharedWriter{

public:

SharedWriter();
~SharedWriter();


/*
* Register a new writable job.
* Thread-safe
*/
void register_job(std::shared_ptr<WriterJob> job);

/*
* This is used by OrderedQueue to notify that head/expected chunk has arrived in the slot.
*  Thread-safe.
*/
void notify_ready(int job_id);


private:

void writer_loop();

private:

std::unordered_map<int, std::shared_ptr<WriterJob>> jobs;
std::queue<int> ready_jobs;

std::mutex mtx;
std::condition_variable cv;
std::thread writer_thread;

bool running = true;

};

