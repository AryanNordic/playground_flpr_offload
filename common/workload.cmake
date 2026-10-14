#
# Copyright (c) 2026 Nordic Semiconductor ASA
#
# SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
#

# Links the workload into the image that runs it: the FLPR image when offloaded,
# the M33 image in the local baseline. Both include this file, so both cores
# build the same loop, the same processing and the same peripheral backend.

set(WORKLOAD_ROOT ${CMAKE_CURRENT_LIST_DIR}/..)

if(CONFIG_APP_TRIGGER_INTERRUPT)
  set(workload_mode irq)
else()
  set(workload_mode poll)
endif()

set(workload_backend
  ${WORKLOAD_ROOT}/peripherals/${CONFIG_APP_PERIPHERAL}/${CONFIG_APP_PERIPHERAL}_${workload_mode}.c)

if(EXISTS ${workload_backend})
  target_sources(app PRIVATE ${workload_backend})
elseif(CONFIG_APP_SENSOR_ACCEL)
  target_sources(app PRIVATE ${WORKLOAD_ROOT}/peripherals/shared/sensor_accel.c)
else()
  message(FATAL_ERROR "No workload backend for peripheral \"${CONFIG_APP_PERIPHERAL}\" "
    "(${workload_mode}): add ${workload_backend}, or enable a shared backend "
    "such as CONFIG_APP_SENSOR_ACCEL in that folder's ${workload_mode}.conf")
endif()

target_sources(app PRIVATE
  ${WORKLOAD_ROOT}/common/workload_loop.c
  ${WORKLOAD_ROOT}/app/workload_process.c)
target_include_directories(app PRIVATE ${WORKLOAD_ROOT}/common ${WORKLOAD_ROOT}/app)
