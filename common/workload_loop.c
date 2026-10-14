/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include <zephyr/kernel.h>
#include <zephyr/sys/util.h>

#include "workload.h"
#include "workload_loop.h"
#include "app_metrics.h"

int workload_loop(uint32_t iterations, uint32_t interval_ms, workload_emit_t emit)
{
	/* Static: the raw batch can be a few hundred bytes. */
	static uint8_t raw[WORKLOAD_RAW_MAX] __aligned(4);
	uint8_t out[WORKLOAD_RESULT_MAX] __aligned(4);
	int ret = 0;

	for (uint32_t seq = 0; iterations == 0 || seq < iterations; seq++) {
		size_t raw_len;
		size_t len;
		uint32_t t0;
		uint32_t t1;
		uint32_t t2;

#if defined(CONFIG_APP_TRIGGER_INTERRUPT)
		/* Sleep until the peripheral has a batch; this wait is not timed. */
		ARG_UNUSED(interval_ms);
		ret = workload_wait();
		if (ret < 0) {
			break;
		}
#else
		if (interval_ms > 0) {
			k_msleep(interval_ms);
		}
#endif

		marker_set(1);
		t0 = k_cycle_get_32();
		raw_len = workload_acquire(raw);
		t1 = k_cycle_get_32();
		if (raw_len == 0) {
			/* Nothing was read; process() never sees an empty batch. */
			marker_set(0);
			continue;
		}
		len = workload_process(raw, MIN(raw_len, sizeof(raw)), out);
		t2 = k_cycle_get_32();
		marker_set(0);

		if (len == 0) {
			continue;
		}

		ret = emit(seq, out, MIN(len, sizeof(out)), cycles_to_us(t1 - t0),
			   cycles_to_us(t2 - t1));
		if (ret < 0) {
			break;
		}
	}

#if defined(CONFIG_APP_TRIGGER_INTERRUPT)
	/* Stop the peripheral so it no longer wakes this core. */
	workload_stop();
#endif

	return ret;
}
