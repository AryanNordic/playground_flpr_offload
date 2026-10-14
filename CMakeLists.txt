#
# Copyright (c) 2026 Nordic Semiconductor ASA
#
# SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
#

cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(flpr_offload)

# app_transmit() is the M33 end of the pipeline in both builds.
target_sources(app PRIVATE src/main.c app/app_transmit.c)
target_include_directories(app PRIVATE common app)

# In the offloaded build the workload (peripheral backend, loop and processing)
# is linked into the FLPR image, and this image only receives its results. In
# the local baseline the same workload sources are linked here instead.
if(CONFIG_APP_OFFLOAD)
  target_sources(app PRIVATE src/offloaded.c)
else()
  target_sources(app PRIVATE src/local.c)
  include(common/workload.cmake)
endif()
