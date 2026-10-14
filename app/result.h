/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#ifndef APP_RESULT_H_
#define APP_RESULT_H_

#include <stdint.h>
#include <zephyr/toolchain.h>

#include "workload.h"

/*
 * The layout of one result from the example processing.
 *
 * workload_process.c writes it and app_transmit.c reads it. Nothing in between
 * looks inside: the loop, the IPC and the M33 forwarding carry it as opaque
 * bytes. When your processing produces something else, change this struct and
 * those two files, and nothing else.
 */

#define RESULT_MAX_CHANNELS 4

struct channel_summary {
	int16_t min;
	int16_t max;
	int16_t mean;
};

struct batch_summary {
	uint16_t samples;  /* samples per channel in the batch */
	uint16_t channels; /* entries of ch[] in use; only those are sent */
	struct channel_summary ch[RESULT_MAX_CHANNELS];
};

BUILD_ASSERT(sizeof(struct batch_summary) <= WORKLOAD_RESULT_MAX,
	     "batch_summary does not fit in WORKLOAD_RESULT_MAX");

#endif /* APP_RESULT_H_ */
