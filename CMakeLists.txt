cmake_minimum_required(VERSION 3.16)

project(huffman LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(
    huffman

    main.cpp
    huffman.cpp
    bit_io.cpp
    helper.cpp

    pipeline/coordinator.cpp
    pipeline/chunk_buffer.cpp
    pipeline/chunk.cpp
    pipeline/frequency_counter.cpp

    threadpool/threadpool.cpp
)

target_include_directories(huffman PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/pipeline
    ${CMAKE_SOURCE_DIR}/threadpool
)

install(TARGETS huffman
        RUNTIME DESTINATION bin)