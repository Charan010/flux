cmake_minimum_required(VERSION 3.16)

project(flux)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_executable(flux

    main.cpp
    helper.cpp

    io/bit_io.cpp

    codecs/huffman/huffman.cpp
    codecs/lz4/lz4.cpp

    pipeline/coordinator.cpp
    pipeline/chunk_buffer.cpp
    pipeline/chunk.cpp

    threadpool/threadpool.cpp

    progress/progress_bar.cpp
    progress/progress_renderer.cpp
)

target_include_directories(flux PRIVATE

    ${CMAKE_SOURCE_DIR}

    ${CMAKE_SOURCE_DIR}/io
    ${CMAKE_SOURCE_DIR}/codecs/huffman
    ${CMAKE_SOURCE_DIR}/codecs/lz4
    ${CMAKE_SOURCE_DIR}/pipeline
    ${CMAKE_SOURCE_DIR}/threadpool
    ${CMAKE_SOURCE_DIR}/progress
    ${CMAKE_SOURCE_DIR}/jobs
     ${CMAKE_SOURCE_DIR}/format
     
)

target_compile_options(flux PRIVATE
    -O3
    -march=native
    -funroll-loops
    -fomit-frame-pointer
    -flto
)

set_property(
    TARGET flux
    PROPERTY INTERPROCEDURAL_OPTIMIZATION TRUE
)

target_link_libraries(flux PRIVATE pthread)

install(TARGETS flux RUNTIME DESTINATION bin)
