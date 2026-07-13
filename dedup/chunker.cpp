#include "chunker.h"

Chunker::Chunker(std::istream& input, size_t window, uint64_t base, uint64_t mask): 
	input_(input), windowSize_(window), base_(base), mask_(mask), highestPower_(1), rolling_hash_(0), eof_(false){

    for (size_t i = 1; i < windowSize_; i++)
        highestPower_ *= base_;
}

bool Chunker::next_chunk(Chunk &chunk){

	chunk.bytes.clear();

	if(eof_)
		return false;

	while(true){

		int value = input_.get();

		if(value == EOF){
			eof_ = true;
			return !chunk.bytes.empty();
		}

		uint8_t byte = static_cast<uint8_t>(value);
		chunk.bytes.push_back(byte);


		if(window_.size() < windowSize_){
			window_.push_back(byte);
			rolling_hash_ = rolling_hash_ * base_ + byte;
			continue;
		}

		uint8_t outgoing = window_.front();
        window_.pop_front();
        window_.push_back(byte);

        rolling_hash_ -= outgoing * highestPower_;
        rolling_hash_*= base_;
        rolling_hash_+= byte;

		if((rolling_hash_ & mask_) == 0)
			return true;
	}
	return false;
}