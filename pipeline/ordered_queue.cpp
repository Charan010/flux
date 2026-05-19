#include "ordered_queue.h"
#include "shared_writer.h"

OrderedQueue::OrderedQueue(uint32_t total_chunks, SharedWriter *writer, int job_id):
    writer_(writer), job_id_(job_id), total_(total_chunks), slots_(total_chunks) {}

void OrderedQueue::push(Chunk chunk){

    bool should_notify = false;

    {
        std::lock_guard<std::mutex> lock(mtx_);
        const uint32_t id = chunk.id;

        slots_[id].chunk = std::move(chunk);
        slots_[id].filled = true;

        if(id == next_expected_ && !scheduled_){
            scheduled_ = true;
            should_notify = true;
        }
    }

    if(should_notify)
        writer_ -> notify_ready(job_id_);

}

std::optional<Chunk> OrderedQueue::try_pop(){

    std::lock_guard<std::mutex> lock(mtx_);

    if(next_expected_ >= total_)
        return std::nullopt;

    if(!slots_[next_expected_].filled)
        return std::nullopt;

    Chunk out = std::move(slots_[next_expected_].chunk);
    slots_[next_expected_].filled = false;

    ++next_expected_;
    return out;

}

void OrderedQueue::close(){

    bool should_notify = false;

    {
        std::lock_guard<std::mutex> lock(mtx_);
        closed_ = true;

        if(!scheduled_){
            scheduled_ = true;
            should_notify = true;            
        }
    }

    if(should_notify)
        writer_ -> notify_ready(job_id_);
}


bool OrderedQueue::is_done() const{

    std::lock_guard<std::mutex> lock(mtx_);
    return closed_ && next_expected_ == total_;
}

void OrderedQueue::clear_scheduled(){

    std::lock_guard<std::mutex> lock(mtx_);
    scheduled_ = false;
}




