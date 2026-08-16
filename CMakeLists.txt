cmake_minimum_required(VERSION 3.20)

project(
    Relic
    VERSION 1.0
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Threads REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(ZSTD REQUIRED libzstd)

add_executable(relic
    main.cpp
    core/snapshot_store.cpp
    hashing/digest.cpp
    chunking/chunker.cpp
    hashing/hasher.cpp
    storage/index.cpp
    storage/pack_format.cpp
    storage/wal.cpp
    io/durability.cpp
    storage/object_store.cpp
    gc/gc.cpp
    io/mmap_file.cpp
    threadpool/threadpool.cpp
    codecs/codec.cpp
)

target_include_directories(relic PRIVATE
    ${PROJECT_SOURCE_DIR}
    ${ZSTD_INCLUDE_DIRS}
)

target_compile_options(relic PRIVATE
    -Wall
    -Wextra
    -Wpedantic
    -Wshadow
    -Wconversion
)

target_compile_options(relic PRIVATE
    $<$<CONFIG:Release>:-O3;-march=native;-DNDEBUG>
    $<$<CONFIG:Debug>:-O0;-g3>
)

target_link_libraries(relic PRIVATE
    Threads::Threads
    blake3
    ${ZSTD_LIBRARIES}
)