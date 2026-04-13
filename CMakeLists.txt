add_executable(flux
    main.cpp
    huffman.cpp
    bit_io.cpp
    pipeline/coordinator.cpp
    pipeline/chunk_buffer.cpp
    pipeline/chunk.cpp
    threadpool/threadpool.cpp
)

target_include_directories(flux PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/pipeline
    ${CMAKE_SOURCE_DIR}/threadpool
)

target_compile_options(flux PRIVATE
    -O3
    -march=native
    -funroll-loops
    -fomit-frame-pointer
    -falign-functions=64
    -falign-loops=64
)

set_property(TARGET flux PROPERTY INTERPROCEDURAL_OPTIMIZATION TRUE)

target_link_libraries(flux PRIVATE pthread)

install(TARGETS flux RUNTIME DESTINATION bin)