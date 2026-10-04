# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Sirac Ozmen
#
# Installs the built library into a prefix under BUILD, then builds and
# runs the external driver against that prefix alone.
set(prefix ${BUILD}/external-prefix)
set(binary ${BUILD}/external-build)
file(REMOVE_RECURSE ${prefix} ${binary})
foreach(step
        "${CMAKE_COMMAND};--install;${BUILD};--prefix;${prefix}"
        "${CMAKE_COMMAND};-S;${SOURCE};-B;${binary};-G;${GENERATOR};-DCMAKE_C_COMPILER=${CC};-DCMAKE_PREFIX_PATH=${prefix};-DCMAKE_BUILD_TYPE=${CONFIG}"
        "${CMAKE_COMMAND};--build;${binary}"
        "${binary}/external_driver")
    execute_process(COMMAND ${step} RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE output)
    if(NOT result EQUAL 0)
        message(FATAL_ERROR "${step} failed (${result}):\n${output}")
    endif()
endforeach()
