#
# Copyright (c) 2026 Nordic Semiconductor ASA
#
# SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
#

# The two build knobs. Both are plain sysbuild variables, for example
#   west build ... -- -DOFFLOAD_PERIPHERAL=twim_lis2dh -DOFFLOAD_TRIGGER=interrupt
# A peripheral is a folder under peripherals/: adding one is adding a folder.
set(OFFLOAD_PERIPHERAL saadc CACHE STRING "Peripheral the workload drives (a folder under peripherals/)")
set(OFFLOAD_TRIGGER timer CACHE STRING "What starts a batch: timer or interrupt")

set(peripheral_dir ${APP_DIR}/peripherals/${OFFLOAD_PERIPHERAL})
if(NOT EXISTS ${peripheral_dir}/peripheral.overlay)
  file(GLOB available RELATIVE ${APP_DIR}/peripherals ${APP_DIR}/peripherals/*/peripheral.overlay)
  list(TRANSFORM available REPLACE "/peripheral.overlay$" "")
  list(JOIN available ", " available)
  message(FATAL_ERROR "OFFLOAD_PERIPHERAL=${OFFLOAD_PERIPHERAL}: no ${peripheral_dir}/peripheral.overlay. "
    "Available: ${available}")
endif()

if(OFFLOAD_TRIGGER STREQUAL "timer")
  set(trigger_conf ${peripheral_dir}/poll.conf)
elseif(OFFLOAD_TRIGGER STREQUAL "interrupt")
  set(trigger_conf ${peripheral_dir}/irq.conf)
else()
  message(FATAL_ERROR "OFFLOAD_TRIGGER=${OFFLOAD_TRIGGER}: use timer or interrupt")
endif()

if(NOT EXISTS ${trigger_conf})
  get_filename_component(trigger_conf_name ${trigger_conf} NAME)
  message(FATAL_ERROR "OFFLOAD_TRIGGER=${OFFLOAD_TRIGGER}: peripherals/${OFFLOAD_PERIPHERAL} "
    "has no ${trigger_conf_name}, so it does not support this trigger")
endif()

# REMOTE_BOARD has a default only for the boards this sample supports.
if("${SB_CONFIG_REMOTE_BOARD}" STREQUAL "")
  message(FATAL_ERROR "Board ${BOARD}${BOARD_QUALIFIERS} is not supported. "
    "This sample builds for nrf54lm20dk/nrf54lm20b/cpuapp.")
endif()

# The offloaded build (any invocation without the "local" file suffix) produces
# both core images from one build command: the Cortex-M33 host and the FLPR
# remote. The "local" configuration runs the workload on the M33 alone, so the
# FLPR image is not built.
if("${FILE_SUFFIX}" STREQUAL "local")
  set(workload_image ${DEFAULT_IMAGE})
else()
  ExternalZephyrProject_Add(
    APPLICATION remote
    SOURCE_DIR ${APP_DIR}/remote
    BOARD ${SB_CONFIG_REMOTE_BOARD}
    BOARD_REVISION ${BOARD_REVISION}
  )

  # Build and flash the FLPR image before the host, so the host can bind to it.
  sysbuild_add_dependencies(CONFIGURE ${DEFAULT_IMAGE} remote)
  sysbuild_add_dependencies(FLASH ${DEFAULT_IMAGE} remote)

  set(workload_image remote)
endif()

# sysbuild keeps these lists in the cache across reconfigures, so take out what an
# earlier configure of this build directory added. Otherwise changing
# OFFLOAD_PERIPHERAL or OFFLOAD_TRIGGER without -p would keep the old files.
foreach(var ${DEFAULT_IMAGE}_EXTRA_DTC_OVERLAY_FILE ${DEFAULT_IMAGE}_EXTRA_CONF_FILE
            remote_EXTRA_DTC_OVERLAY_FILE remote_EXTRA_CONF_FILE)
  if(DEFINED CACHE{${var}} AND DEFINED CACHE{OFFLOAD_ADDED_FILES})
    set(value "$CACHE{${var}}")
    list(REMOVE_ITEM value ${OFFLOAD_ADDED_FILES})
    sysbuild_cache_set(VAR ${var} ${value})
  endif()
endforeach()
set(OFFLOAD_ADDED_FILES ${peripheral_dir}/hand_to_flpr.overlay ${peripheral_dir}/peripheral.overlay
    ${trigger_conf} CACHE INTERNAL "Files this sample added to the image lists")

if(NOT "${FILE_SUFFIX}" STREQUAL "local")
  # The M33 hands the peripheral over to the FLPR.
  sysbuild_cache_set(VAR ${DEFAULT_IMAGE}_EXTRA_DTC_OVERLAY_FILE APPEND REMOVE_DUPLICATES
    ${peripheral_dir}/hand_to_flpr.overlay)
endif()

# The core that runs the workload gets the peripheral and its driver.
sysbuild_cache_set(VAR ${workload_image}_EXTRA_DTC_OVERLAY_FILE APPEND REMOVE_DUPLICATES
  ${peripheral_dir}/peripheral.overlay)
sysbuild_cache_set(VAR ${workload_image}_EXTRA_CONF_FILE APPEND REMOVE_DUPLICATES
  ${trigger_conf})

# Both images learn the selection: the workload image to pick its sources, the
# M33 to name the build in its log.
set(images ${DEFAULT_IMAGE} ${workload_image})
list(REMOVE_DUPLICATES images)
foreach(image ${images})
  set_config_string(${image} CONFIG_APP_PERIPHERAL "${OFFLOAD_PERIPHERAL}")
  if(OFFLOAD_TRIGGER STREQUAL "interrupt")
    set_config_bool(${image} CONFIG_APP_TRIGGER_INTERRUPT y)
  else()
    set_config_bool(${image} CONFIG_APP_TRIGGER_TIMER y)
  endif()
endforeach()
