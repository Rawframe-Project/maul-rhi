# The seam with Maul Window (mrhi-0028), test only: Maul Window at its
# release tag, fetched beside this check and linked into it alone (a
# local copy may stand in through FETCHCONTENT_SOURCE_DIR_MAUL-WINDOW).
# The library and its install never see it.

include(FetchContent)
FetchContent_Declare(maul-window
    GIT_REPOSITORY https://github.com/Rawframe-Project/maul-window.git
    GIT_TAG v0.10.0
    GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(maul-window)

add_executable(test_seam seam.c ${PROJECT_SOURCE_DIR}/samples/harness.c)
target_include_directories(test_seam PRIVATE ${PROJECT_SOURCE_DIR}/samples)
target_link_libraries(test_seam PRIVATE maul-rhi maul-window::maul-window)
maul_apply_flags(test_seam)
if(EMSCRIPTEN)
    # In headless Chrome on the runner's page, as the samples run.
    target_link_options(test_seam PRIVATE -sJSPI -sEXIT_RUNTIME -sEXPORTED_RUNTIME_METHODS=ENV)
    add_test(NAME seam COMMAND node ${PROJECT_SOURCE_DIR}/test/web_runner.cjs
                               $<TARGET_FILE:test_seam>)
else()
    add_test(NAME seam COMMAND test_seam)
endif()
set_tests_properties(seam PROPERTIES SKIP_RETURN_CODE 77 ENVIRONMENT "${maul_rhi_test_env}"
                     FAIL_REGULAR_EXPRESSION "Validation Error")
