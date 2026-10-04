# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Sirac Ozmen
#
# A driver built outside the tree against an installed Maul RHI
# (mrhi-0024): find_package and the installed headers only.
cmake_minimum_required(VERSION 3.25)
project(maul_rhi_external_driver C)
set(CMAKE_C_STANDARD 23)
set(CMAKE_C_STANDARD_REQUIRED ON)
find_package(maul-rhi REQUIRED CONFIG)
add_executable(external_driver driver.c)
target_link_libraries(external_driver PRIVATE maul-rhi::maul-rhi)
