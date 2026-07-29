#include "chunker.h"


constexpr size_t kMaxChunk = 1u << 20;   // 1 MB hard cap
constexpr size_t kMinChunk = 2u << 10; //2 KB minimum chunk

Chunker::Chunker(const uint8_t *data, size_t size, size_t window, uint64_t base, uint64_t mask)
    : data_(data), size_(size), pos_(0), window_(window), base_(base), mask_(mask), highest_power_(1){

    for (size_t i = 1; i < window_; i++)
        highest_power_ *= base_;
}

/*
 * Extracts the next content-defined chunk.

 * @param chunk Recieves the extracted chunk
 * @return true if a chunk is produced. false if the input stream ends.

*/
bool Chunker::next_chunk(Chunk &chunk){


    chunk.bytes.clear();
    if (pos_ >= size_)
        return false;

    size_t start = pos_;
    uint64_t hash = 0;

    while (pos_ < size_) {
        uint8_t incoming = data_[pos_++];
        size_t window_len = pos_ - start;

        if (window_len <= window_)
            hash = hash * base_ + incoming;

        else{
        	uint8_t outgoing = data_[pos_ - window_ - 1];
            hash -= outgoing * highest_power_;
            hash *= base_;
            hash += incoming;
        }

        if(window_len >= kMinChunk && ((hash & mask_) == 0 || window_len >= kMaxChunk))
    		break; 
    }

    chunk.bytes.assign(data_ + start, data_ + pos_);
    return true;
}
