cmake_minimum_required(VERSION 3.20)

project(
    Flux
    VERSION 1.0
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(flux
    main.cpp
    core/dedup_engine.cpp
    chunking/chunker.cpp
    hashing/hasher.cpp
    storage/index.cpp
    storage/object_store.cpp
    gc/gc.cpp
    io/mmap_file.cpp
    threadpool/threadpool.cpp
    codecs/lz4/lz4.cpp
)

target_include_directories(flux PRIVATE
    ${PROJECT_SOURCE_DIR}
)

target_compile_options(flux PRIVATE
    -Wall
    -Wextra
    -Wpedantic
    -Wshadow
    -Wconversion
)

target_compile_options(flux PRIVATE
    $<$<CONFIG:Release>:-O3;-march=native;-DNDEBUG>
    $<$<CONFIG:Debug>:-O0;-g3>
)

find_package(Threads REQUIRED)

target_link_libraries(flux PRIVATE
    Threads::Threads
    blake3
)