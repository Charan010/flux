#include "frequency_counter.h"

/*
    shifting from hashmap to array because if im expecting a multibyte characters. frequency is counted based
    on individual bytes only

    since bytes range is 0 to 255 its just far more simpler to use array of 256 size than hashmap which increases
    some complexity.


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


    /*the main thread which called will keep on sleeping until number of chunks left reaches
    0 and gets woken up by done_cv condition variable */

    done_cv.wait(lock, [this]{
        return pending == 0;
    });

}

FrequencyTable FrequencyCounter::get_result(){
    return global_freq;
}