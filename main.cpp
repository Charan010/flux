#include <iostream>

#include "bench.h"
#include "coordinator.h"
#include "daemon.h"
#include "jobs/jobs_common.h"

int main(int argc, char *argv[]) {

  	Coordinator coordinator(Config::threadpool_size);
  	constexpr size_t chunk_size = Config::chunk_size;

  	if (argc > 2 && std::string(argv[1]) == "--b"){

    	Threadpool bench_pool(Config::threadpool_size);
    	run_bench_mode(argv[2], bench_pool);

		return 0;
  	} 

	run_daemon(coordinator, chunk_size);
  	return 0;
}
