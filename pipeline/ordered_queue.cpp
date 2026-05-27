#include "ordered_queue.h"
#include "shared_writer.h"

OrderedQueue::OrderedQueue(size_t total_chunks, SharedWriter *writer,
                           uint64_t job_id)
    : total_chunks_(total_chunks), job_id_(job_id), shared_writer_(writer) {


  pending.reserve(total_chunks);
}

void OrderedQueue::push(Chunk chunk) {

  const uint32_t id = chunk.id;

  {
    std::lock_guard lock(mtx);

    auto [it, inserted] = pending.emplace(id, std::move(chunk));
    if (!inserted)
      throw std::runtime_error("duplicate chunk id");
  }

  if (shared_writer_)
    shared_writer_->notify();
}

bool OrderedQueue::try_pop(Chunk &out) {

  std::lock_guard lock(mtx);

  auto it = pending.find(expected);
  if (it == pending.end())
    return false;

  out = std::move(it->second);
  pending.erase(it);
  ++expected;

  return true;
}

bool OrderedQueue::is_done() {

  std::lock_guard lock(mtx);
  return done && pending.empty();
}

void OrderedQueue::close() {

  {
    std::lock_guard lock(mtx);
    done = true;
  }

  if (shared_writer_)
    shared_writer_->notify();
}
