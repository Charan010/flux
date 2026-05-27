#pragma once

#include "cli.h"
#include "threadpool.h"
#include <string>

void run_bench_mode(const std::string &input_path, Threadpool &pool);
