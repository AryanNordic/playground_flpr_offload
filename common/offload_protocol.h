/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#ifndef APP_OFFLOAD_PROTOCOL_H_
#define APP_OFFLOAD_PROTOCOL_H_

#include <stdint.h>
#include "workload.h"

/*
 * Inter-core data contract between the Cortex-M33 and the FLPR.
 *
 * The M33 sends one control message to start the run. After that only the FLPR
 * sends: one result per batch, plus status and error messages. The M33 never
 * sends anything per batch.
 *
 * A result is opaque. It carries the bytes workload_process() produced and its
 * length; the transport does not interpret it. That is what keeps the workload
 * swappable without editing this file: a different workload changes only the
 * bytes inside, never the contract around them.
 *
 * Both cores are little-endian (Cortex-M and this RISC-V VPR), so the struct is
 * exchanged as raw bytes with no serialisation. Keep every field naturally
 * aligned if you extend it, and change both images together.
 */

enum offload_msg_type {
	OFFLOAD_MSG_CONTROL = 1, /* M33 -> FLPR: start the run */
	OFFLOAD_MSG_STATUS = 2,  /* FLPR -> M33: lifecycle transitions */
	OFFLOAD_MSG_RESULT = 3,  /* FLPR -> M33: one unit of work */
	OFFLOAD_MSG_ERROR = 4,   /* FLPR -> M33: fault, run aborted */
};

enum offload_status {
	OFFLOAD_STATUS_RUNNING = 2,
	OFFLOAD_STATUS_DONE = 3,
};

struct offload_control {
	uint32_t iterations;         /* batches to run, 0 runs continuously */
	uint32_t sample_interval_ms; /* delay between batches, 0 samples continuously */
};

struct offload_result {
	uint32_t iteration;                 /* batch index, also the sequence number */
	uint32_t acquire_us;                /* FLPR time in workload_acquire() */
	uint32_t process_us;                /* FLPR time in workload_process() */
	uint16_t len;                       /* number of valid bytes in "bytes" */
	uint8_t bytes[WORKLOAD_RESULT_MAX]; /* opaque result from workload_process() */
};

struct offload_msg {
	uint8_t type; /* enum offload_msg_type */
	uint8_t reserved[3];
	union {
		struct offload_control control;
		struct offload_result result;
		uint32_t status; /* enum offload_status */
		int32_t error;   /* negative errno */
	};
};

#endif /* APP_OFFLOAD_PROTOCOL_H_ */
