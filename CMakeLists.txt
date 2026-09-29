cmake_minimum_required(VERSION 3.25)

# The library's identity. The family's CMake modules and tools read these.
set(MAUL_LIBRARY maul-rhi)
set(MAUL_API_PREFIX mrhi)
set(MAUL_MACRO_PREFIX MRHI)
set(MAUL_TITLE "Maul RHI")
set(MAUL_PREFIX MAUL_RHI)

include(cmake/ReadVersion.cmake)
maul_read_version(${CMAKE_CURRENT_SOURCE_DIR}/include/maul-rhi/base.h MRHI MAUL_VERSION)
project(maul-rhi
    VERSION ${MAUL_VERSION}
    DESCRIPTION "The GPU device layer: devices, resources, a frame graph and presentation"
    LANGUAGES C)

option(MAUL_RHI_BUILD_TESTS "Build the test suites" ${PROJECT_IS_TOP_LEVEL})
option(MAUL_RHI_BUILD_BENCH "Build the benchmarks" ${PROJECT_IS_TOP_LEVEL})
option(MAUL_RHI_BUILD_SHARED "Build a shared library instead of a static one" OFF)
option(MAUL_RHI_INSTALL "Generate install rules" ${PROJECT_IS_TOP_LEVEL})
option(MAUL_RHI_WERROR "Treat compiler warnings as errors" OFF)
option(MAUL_RHI_SANITIZE "Instrument with AddressSanitizer and UBSan" OFF)
option(MAUL_RHI_TSAN "Instrument with ThreadSanitizer" OFF)
option(MAUL_RHI_COVERAGE "Instrument for gcov coverage" OFF)
option(MAUL_RHI_TEST_DRIVER "Build the test driver (mrhiTestDriverDef)" ${MAUL_RHI_BUILD_TESTS})
# The Vulkan driver (mrhi-0003): every native build; the web has no Vulkan.
if(EMSCRIPTEN)
    set(maul_rhi_vulkan_default OFF)
else()
    set(maul_rhi_vulkan_default ON)
endif()
option(MAUL_RHI_VULKAN_DRIVER "Build the Vulkan driver" ${maul_rhi_vulkan_default})

# Every executable (tests, benchmarks, tools) lands in the top of the
# build tree, so scripts and CI find them in one place.
if(PROJECT_IS_TOP_LEVEL)
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR})
endif()

include(GNUInstallDirs)
include(cmake/CompilerFlags.cmake)
include(cmake/Library.cmake)
include(cmake/Testing.cmake)

if(MAUL_RHI_BUILD_SHARED)
    set(MAUL_LIBRARY_KIND SHARED)
else()
    set(MAUL_LIBRARY_KIND STATIC)
endif()
add_library(maul-rhi ${MAUL_LIBRARY_KIND}
    src/adapter.c
    src/allocator.c
    src/base.c
    src/cache.c
    src/chain.c
    src/compile.c
    src/container.c
    src/device.c
    src/binding.c
    src/copy.c
    src/upload.c
    src/query.c
    src/readback.c
    src/submit.c
    src/draw.c
    src/encoder.c
    src/frame.c
    src/frame_resource.c
    src/driver.c
    src/generated/capabilities.c
    src/generated/defaults.c
    src/generated/result_names.c
    src/graphics.c
    src/instance.c
    src/label.c
    src/memory.c
    src/notify.c
    src/pass.c
    src/pipeline.c
    src/plan.c
    src/pool.c
    src/reflection.c
    src/resources.c
    src/sha256.c
    src/shader.c
    src/surface.c
    src/swapchain.c
    src/texture.c
    src/tlsf.c
    src/view.c)
maul_configure_library(maul-rhi)
if(MAUL_RHI_TEST_DRIVER)
    target_sources(maul-rhi PRIVATE src/driver_test.c src/driver_test_frame.c)
    target_compile_definitions(maul-rhi PRIVATE MAUL_RHI_TEST_DRIVER)
endif()
if(MAUL_RHI_VULKAN_DRIVER)
    target_sources(maul-rhi PRIVATE src/driver_vulkan.c src/vulkan_adapter.c src/vulkan_api.c
                                   src/vulkan_device.c src/vulkan_frame.c src/vulkan_graphics.c
                                   src/vulkan_memory.c src/vulkan_object.c src/vulkan_pass.c src/vulkan_pipeline.c
                                   src/vulkan_record.c src/vulkan_resource.c)
    target_compile_definitions(maul-rhi PRIVATE MAUL_RHI_VULKAN_DRIVER)
    # The Khronos headers, kept as published (khronos/README.md).
    target_include_directories(maul-rhi SYSTEM PRIVATE ${PROJECT_SOURCE_DIR}/khronos)
    target_link_libraries(maul-rhi PRIVATE ${CMAKE_DL_LIBS})
endif()

if(MAUL_RHI_BUILD_TESTS)
    enable_testing()
    maul_add_test(base)
    maul_add_test(chain WHITEBOX)
    maul_add_test(layout WHITEBOX)
    maul_add_test(notify WHITEBOX)
    maul_add_test(pool WHITEBOX)
    maul_add_test(label WHITEBOX)
    maul_add_test(sha256 WHITEBOX)
    maul_add_test(tlsf WHITEBOX)
    maul_add_test(instance)
    if(MAUL_RHI_TEST_DRIVER)
        maul_add_test(adapter)
        maul_add_test(device)
        maul_add_test(format)
        maul_add_test(sampler)
        maul_add_test(buffer)
        maul_add_test(texture)
        maul_add_test(view)
        maul_add_test(surface)
        maul_add_test(swapchain)
        maul_add_test(frame)
        maul_add_test(frame_resource)
        maul_add_test(pass)
        maul_add_test(plan)
        maul_add_test(memory)
        maul_add_test(shader WHITEBOX)
        maul_add_test(pipeline WHITEBOX)
        maul_add_test(graphics WHITEBOX)
        maul_add_test(cache WHITEBOX)
        maul_add_test(encoder WHITEBOX)
        maul_add_test(binding WHITEBOX)
        maul_add_test(draw WHITEBOX)
        maul_add_test(copy WHITEBOX)
        maul_add_test(upload WHITEBOX)
        maul_add_test(query WHITEBOX)
        maul_add_test(readback WHITEBOX)
        maul_add_test(submit WHITEBOX)
        maul_add_test(acquire WHITEBOX)
        maul_add_test(loss WHITEBOX)
        # The container writer against the reader, where Python runs.
        find_package(Python3 COMPONENTS Interpreter)
        if(TARGET test_shader AND Python3_Interpreter_FOUND AND NOT CMAKE_CROSSCOMPILING)
            add_test(NAME container_writer
                     COMMAND Python3::Interpreter ${PROJECT_SOURCE_DIR}/test/test_container_writer.py
                             $<TARGET_FILE:test_shader>)
        endif()
    endif()
    # Every driver the build has, through the public API (mrhi-0003).
    maul_add_test(conformance)
    maul_add_header_tests()
    # The Vulkan loader keeps drivers loaded after an instance ends, so
    # that LeakSanitizer reads their memory as held, not lost. Where the
    # validation layer runs, it logs to the test's output, and a
    # validation error there fails the test (mrhi-0003).
    get_property(maul_rhi_tests DIRECTORY PROPERTY TESTS)
    set_tests_properties(${maul_rhi_tests} PROPERTIES
                         ENVIRONMENT VK_LOADER_DISABLE_DYNAMIC_LIBRARY_UNLOADING=1
                         FAIL_REGULAR_EXPRESSION "Validation Error")
endif()

if(MAUL_RHI_BUILD_BENCH AND EXISTS ${PROJECT_SOURCE_DIR}/bench/bench_main.c)
    add_subdirectory(bench)
endif()

if(MAUL_RHI_INSTALL)
    include(cmake/Install.cmake)
endif()
