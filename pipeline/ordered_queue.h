#pragma once

#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <vector>

#include "chunk.h"


/*

* OrderedQueue:
* Stores out-of-order produced chunks and exposes in-order consumption.
*
*
*
* Contract:
- Producers may push computed chunks concurrently.
- Consumer/ Writer thread is single threaded.
- Chunks are written only in ascending order of chunk id.
- Queue notifies SharedWriter when correct chunk has arrived which has to be written to file.

*/


class OrderedQueue{

public:

OrderedQueue(uint32_t total_chunks, SharedWriter *writer, int job_id);


/*
* push computed chunk into its indexed slot.
*/
void push(Chunk chunk);


/*
* Try popping out next chunk in-order.
*
* Returns:
* - Chunk if next expected chunk is ready in its slot.
* - std::nullopt otherwise
*   This is non blocking, if the chunk is not ready, writer just continues sleeping until it gets notified.

*/
std::optional<Chunk> try_pop();


/*
* Marks producer side complete.
* This is thread-safe.
*/
void close();
bool is_done() const;

void clear_scheduled();

private:

struct Slot{
    Chunk chunk;
    bool filled = false;
};


private:

SharedWriter *writer_ ;
int job_id_;


uint32_t total_;
uint32_t next_expected_;

bool closed_ = false;
bool scheduled_ = false;

std::vector<Slot> slots_;
mutable std::mutex mtx_;

};