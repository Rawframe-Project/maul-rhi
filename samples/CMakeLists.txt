# The samples: small programs that check their own results, so that each
# is also a test on the build's native driver, run as the conformance
# suite runs (in headless Chrome on the web). A sample exits 77 when
# there is no adapter and none is required.

function(maul_rhi_add_sample name)
    set(target sample_${name})
    add_executable(${target} ${name}.c harness.c)
    target_link_libraries(${target} PRIVATE maul-rhi)
    target_include_directories(${target} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
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
