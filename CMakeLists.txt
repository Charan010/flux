cmake_minimum_required(VERSION 3.16)

project(flux)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_executable(flux

    main.cpp

    io/bit_io.cpp
    io/mmap_file.cpp

    codecs/huffman/huffman.cpp
    codecs/huffman/huffman_engine.cpp
    codecs/lz4/lz4.cpp
    codecs/lz4/LZ4_engine.cpp

    pipeline/coordinator.cpp
    pipeline/chunk.cpp
    pipeline/ordered_queue.cpp
    pipeline/shared_writer.cpp

    jobs/compression_job.cpp
    jobs/decompression_job.cpp

    threadpool/threadpool.cpp

    daemon/daemon.cpp
	
    CLI/cli.cpp
	
    benchmark/bench.cpp
)

target_include_directories(flux PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/io
    ${CMAKE_SOURCE_DIR}/codecs/huffman
    ${CMAKE_SOURCE_DIR}/codecs/lz4
    ${CMAKE_SOURCE_DIR}/pipeline
    ${CMAKE_SOURCE_DIR}/threadpool
    ${CMAKE_SOURCE_DIR}/jobs
	${CMAKE_SOURCE_DIR}/daemon
	${CMAKE_SOURCE_DIR}/benchmark
	${CMAKE_SOURCE_DIR}/CLI
)

target_compile_options(flux PRIVATE
    $<$<CONFIG:Release>:
        -O3
        -march=native
        -funroll-loops
        -fomit-frame-pointer
        -DNDEBUG
    >

    $<$<CONFIG:Debug>:
        -O1
        -g
        -fsanitize=address
        -fno-omit-frame-pointer
    >
)

target_link_options(flux PRIVATE  $<$<CONFIG:Debug>:-fsanitize=address>)

set_property(TARGET flux PROPERTY INTERPROCEDURAL_OPTIMIZATION TRUE)
target_link_libraries(flux PRIVATE pthread)
install(TARGETS flux RUNTIME DESTINATION bin)
