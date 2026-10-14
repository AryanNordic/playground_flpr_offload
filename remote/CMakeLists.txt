#
# Copyright (c) 2026 Nordic Semiconductor ASA
#
# SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
#

cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(flpr_offload_remote)

# The transport to the M33 is the only FLPR-specific source. The workload (the
# peripheral backend, the batch loop and the processing) comes from the same
# file the M33 uses in the local baseline.
target_sources(app PRIVATE src/main.c)
include(${CMAKE_CURRENT_SOURCE_DIR}/../common/workload.cmake)
