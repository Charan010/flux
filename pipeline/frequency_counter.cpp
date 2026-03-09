#include "frequency_counter.h"

/*
    This implementation just consumes byte wise . Since byte range is 0 to 255,
    array of size 256 is much more optimal than using unordered_map because we know the range.
*/



FrequencyCounter::FrequencyCounter(ThreadPool &p) : pool(p){
    pending = 0;
    global_freq.fill(0);
}


void FrequencyCounter::submit_chunk(const Chunk& chunk){

    pending++;

    pool.submit([this, chunk]{

        FrequencyTable local{};
        local.fill(0);

        for(unsigned char c : chunk.data)
            local[c]++;

        {
            std::lock_guard<std::mutex> lock(freq_mtx);
            for(int i = 0; i < 256; ++i)
                global_freq[i] += local[i];
        }

        pending--;
        done_cv.notify_one();  
    });
}


void FrequencyCounter::wait(){

    std::unique_lock<std::mutex> lock(done_mtx);


    // The caller thread sleep until the chunks have merged their local frequency table with global state
    // and then wakes up the caller thread with done_cv condition variable.

    done_cv.wait(lock, [this]{
        return pending == 0;
    });

}

FrequencyTable FrequencyCounter::get_result(){
    return global_freq;
}