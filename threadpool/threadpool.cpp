#include "threadpool.h"

ThreadPool::ThreadPool(size_t num_threads) : stop(false){
    
    for(size_t i=0;i<num_threads;i++)
        workers.emplace_back(&ThreadPool::worker_loop,this);

    
}

ThreadPool::~ThreadPool(){
    shutdown();
}

void ThreadPool::submit(std::function<void()> job){   
    
    // kind of exploiting RAAI in C++ . Lock gets automatically released the moment,
    // it goes out of scope.
    
    {
        std::lock_guard<std::mutex> lock(mtx);
        jobs.push(std::move(job));
    }

    cv.notify_one();
}

void ThreadPool::shutdown(){

    {
        std::lock_guard<std::mutex> lock(mtx);
        stop = true;
    }

    cv.notify_all();

    for(auto &t : workers){

        if(t.joinable())
            t.join();
    }
}


void ThreadPool::worker_loop(){
    while(true){
        
        std::function<void()> job;

        {
            std::unique_lock<std::mutex> lock(mtx);

            cv.wait(lock,[this]{
                return stop || !jobs.empty();
            });

            if(stop && jobs.empty())
                return;

            job = std::move(jobs.front());
            jobs.pop();
        }

        job();
    }
}