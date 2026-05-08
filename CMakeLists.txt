add_executable(flux
    main.cpp
    huffman.cpp
    bit_io.cpp
    pipeline/coordinator.cpp
    pipeline/chunk_buffer.cpp
    pipeline/chunk.cpp
    threadpool/threadpool.cpp
    progress/progress_bar.cpp
    progress/progress_renderer.cpp
    helper.cpp
)

target_include_directories(flux PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/pipeline
    ${CMAKE_SOURCE_DIR}/threadpool
    ${CMAKE_SOURCE_DIR}/progress
)

target_compile_options(flux PRIVATE
    -O3
    -march=native
    -fomit-frame-pointer
    -g
)

set_property(TARGET flux PROPERTY INTERPROCEDURAL_OPTIMIZATION TRUE)

target_link_libraries(flux PRIVATE pthread)

install(TARGETS flux RUNTIME DESTINATION bin)