# The benchmarks: never a CTest gate, because wall time is not
# repeatable. They print their timings (conventions, section 14), on the
# build's native driver through the samples' harness, and not on the
# web, whose timers and pacing are the browser's.
if(EMSCRIPTEN OR CMAKE_SYSTEM_NAME STREQUAL "WASI")
    return()
endif()
add_executable(${PROJECT_NAME}_bench bench_main.c ${PROJECT_SOURCE_DIR}/samples/harness.c)
target_link_libraries(${PROJECT_NAME}_bench PRIVATE ${PROJECT_NAME})
target_include_directories(${PROJECT_NAME}_bench PRIVATE ${PROJECT_SOURCE_DIR}/samples)
maul_apply_flags(${PROJECT_NAME}_bench)
