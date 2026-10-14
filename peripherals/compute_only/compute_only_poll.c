/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

/*
 * No peripheral: the starting point when the work to offload is computation on
 * data the application already has, not a sensor read.
 *
 * workload_acquire() fills the batch with a fixed ramp. Put your own input here
 * instead, for example a copy of a buffer the M33 filled. The ramp repeats the
 * same values on both cores, so the offloaded and the baseline builds log the
 * same results and can be compared directly.
 */

#include <stdint.h>

#include <zephyr/toolchain.h>

#include "workload.h"

#define SAMPLES_PER_BATCH 64

BUILD_ASSERT(SAMPLES_PER_BATCH * sizeof(int16_t) <= WORKLOAD_RAW_MAX,
	     "the batch does not fit in WORKLOAD_RAW_MAX");

int workload_init(void)
{
	return 0;
}

size_t workload_acquire(void *raw)
{
	static uint32_t batch;
	int16_t *samples = raw;

	for (int i = 0; i < SAMPLES_PER_BATCH; i++) {
		samples[i] = (int16_t)((int32_t)((batch * SAMPLES_PER_BATCH + i) % 1000) - 500);
	}
	batch++;

	return SAMPLES_PER_BATCH * sizeof(int16_t);
}
