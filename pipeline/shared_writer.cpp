#include "shared_writer.h"

#include "compression_job.h"
#include "huffman.h"

SharedWriter::SharedWriter()
    : writer_thread(&SharedWriter::writer_loop, this) {}

SharedWriter::~SharedWriter() {

  {
    std::lock_guard lock(mtx);
    running = false;
  }

  cv.notify_one();

  if (writer_thread.joinable())
    writer_thread.join();
}

void SharedWriter::register_job(std::shared_ptr<WriterJob> job) {

  std::lock_guard lock(mtx);
  jobs[job->id] = std::move(job);
  cv.notify_one();
  
}

void SharedWriter::notify(){
	 cv.notify_one(); 
}

void SharedWriter::write_chunk(WriterJob &job, const Chunk &chunk){
	if(job.raw_output)
		job.writer -> write_bytes(chunk.data);

	else{
		write_uint32(*job.writer, chunk.compressed_bytes);
		write_uint32(*job.writer, chunk.bit_count);
		job.writer -> write_bytes(chunk.data);
	}

}

bool SharedWriter::drain_queue(WriterJob &job, std::unique_lock<std::mutex> &lock){

	bool wrote_any = false;

	while(true){

		Chunk chunk;

		if(!job.queue -> try_pop(chunk))
			break;

		wrote_any = true;

		lock.unlock();
		write_chunk(job, chunk);
		lock.lock();

	}

	return wrote_any;
}

void SharedWriter::finish_job(WriterJob &job, std::unique_lock<std::mutex> &lock){

	lock.lock();

	job.writer -> flush();

	if(job.on_complete)
		job.on_complete();

	lock.unlock();
}

bool SharedWriter::process_job(WriterJob &job, std::unique_lock<std::mutex> &lock){

	bool progress = drain_queue(job, lock);

	if(!job.queue -> is_done())
		return progress;

	progress |= drain_queue(job, lock);

	finish_job(job, lock);
	return true;

}


void SharedWriter::writer_loop(){

	while(true){

		std::unique_lock lock(mtx);

		cv.wait(lock, [this] {
			return !running || !jobs.empty();
		});

		if(!running && !jobs.empty())
			break;

		bool made_progress;

		do{
			made_progress = false;
			for(auto it = jobs.begin(); it != jobs.end(); ){

				auto &job = it -> second;

				if(process_job(*job, lock)){

					made_progress = true;

					if(job ->queue -> is_done()){
						it = jobs.erase(it);
						continue;
					}
				}

				++it;

			}

		}while(made_progress);

	}

}
