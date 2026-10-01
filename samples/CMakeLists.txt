# The samples: small programs that check their own results, so that each
# is also a test on the build's native driver, run as the conformance
# suite runs (in headless Chrome on the web). A sample exits 77 when
# there is no adapter and none is required.

# The presentation samples' windows: XCB, where the Vulkan driver is
# built natively and the XCB client library installed, Win32 windows on the D3D12
# driver, the web runner's canvases, or CAMetalLayers outside any window
# on the Metal driver.
if(MAUL_RHI_VULKAN_DRIVER AND NOT CMAKE_CROSSCOMPILING)
    find_package(PkgConfig QUIET)
    if(PkgConfig_FOUND)
        pkg_check_modules(MAUL_RHI_SAMPLE_XCB QUIET IMPORTED_TARGET xcb)
    endif()
endif()

# maul_rhi_add_sample(<name> [WINDOW]): samples/<name>.c on the harness,
# with the window shim when it presents.
function(maul_rhi_add_sample name)
    cmake_parse_arguments(ARG "WINDOW" "" "" ${ARGN})
    set(target sample_${name})
    add_executable(${target} ${name}.c harness.c)
    target_link_libraries(${target} PRIVATE maul-rhi)
    target_include_directories(${target} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    if(ARG_WINDOW)
        target_sources(${target} PRIVATE window.c)
        if(MAUL_RHI_SAMPLE_XCB_FOUND)
            target_link_libraries(${target} PRIVATE PkgConfig::MAUL_RHI_SAMPLE_XCB)
            target_compile_definitions(${target} PRIVATE SAMPLE_XCB)
        endif()
        if(MAUL_RHI_D3D12_DRIVER)
            target_compile_definitions(${target} PRIVATE SAMPLE_WIN32)
        endif()
        if(MAUL_RHI_METAL_DRIVER)
            target_sources(${target} PRIVATE ${PROJECT_SOURCE_DIR}/test/metal_layer.m)
            target_include_directories(${target} PRIVATE ${PROJECT_SOURCE_DIR}/test)
            set_target_properties(${target} PROPERTIES OBJC_STANDARD 23 OBJC_STANDARD_REQUIRED ON)
            target_compile_options(${target} PRIVATE $<$<COMPILE_LANGUAGE:OBJC>:-fno-objc-arc>)
            target_compile_definitions(${target} PRIVATE SAMPLE_METAL)
            target_link_libraries(${target} PRIVATE "-framework QuartzCore")
        endif()
    endif()
    maul_apply_flags(${target})
    if(NOT MAUL_RHI_BUILD_TESTS)
        return()
    endif()
    if(EMSCRIPTEN)
        target_link_options(${target} PRIVATE -sJSPI -sEXIT_RUNTIME -sEXPORTED_RUNTIME_METHODS=ENV)
        add_test(NAME ${target}
                 COMMAND node ${PROJECT_SOURCE_DIR}/test/web_runner.cjs $<TARGET_FILE:${target}>)
    else()
        add_test(NAME ${target} COMMAND ${target})
    endif()
    set_tests_properties(${target} PROPERTIES SKIP_RETURN_CODE 77
                         ENVIRONMENT "${maul_rhi_test_env}"
                         FAIL_REGULAR_EXPRESSION "Validation Error")
endfunction()

maul_rhi_add_sample(triangle)
maul_rhi_add_sample(upload_readback)
maul_rhi_add_sample(textured)
maul_rhi_add_sample(compute_indirect)
maul_rhi_add_sample(msaa)
maul_rhi_add_sample(shadow)
maul_rhi_add_sample(hdr_surface WINDOW)
maul_rhi_add_sample(two_windows WINDOW)
maul_rhi_add_sample(present_states WINDOW)
maul_rhi_add_sample(device_loss)
