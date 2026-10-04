# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Sirac Ozmen
#
# The mustpass list of the SPI version against the conformance suite's
# own listing (mrhi-0024): a case added, renamed or dropped in the suite
# must be recorded in the list, and a new SPI version needs its list.
execute_process(COMMAND ${SUITE} --list RESULT_VARIABLE result OUTPUT_VARIABLE listed)
if(NOT result EQUAL 0)
    message(FATAL_ERROR "${SUITE} --list failed (${result})")
endif()
if(NOT EXISTS ${LIST})
    message(FATAL_ERROR "no mustpass list ${LIST}; write it from ${SUITE} --list")
endif()
file(READ ${LIST} recorded)
if(NOT listed STREQUAL recorded)
    message(FATAL_ERROR "${LIST} differs from ${SUITE} --list:\n${listed}")
endif()
