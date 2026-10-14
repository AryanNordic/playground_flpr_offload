/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

/*
 * Your M33 code: app_transmit(), what happens to each result.
 *
 * Every result ends here, on the M33, in both builds. In the offloaded build it
 * came from the FLPR over IPC; in the baseline build the M33 produced it itself.
 * Replace the body with your radio send (BLE, Thread, Zigbee) or whatever your
 * application does with a result.
 *
 * It runs in a normal thread, so it may block. In the offloaded build the FLPR
 * keeps working while it blocks, and up to eight results wait in a queue; beyond
 * that results are dropped and the M33 report counts them. In the baseline build
 * the next batch waits until it returns, as it would in a single-core design.
 */

#include <stddef.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "result.h"
#include "workload.h"

LOG_MODULE_REGISTER(app, LOG_LEVEL_INF);

void app_transmit(uint32_t seq, const void *payload, size_t len)
{
	/*
	 * EXAMPLE, replace the body with your send.
	 *
	 * It decodes the result with app/result.h and logs one result per report
	 * interval, so you can see real values without the log itself loading the
	 * M33 at high batch rates.
	 */
	static int64_t next_log_ms;
	struct batch_summary res = {0};
	const size_t header = offsetof(struct batch_summary, ch);
	int64_t now = k_uptime_get();

	if (now < next_log_ms) {
		return;
	}
	next_log_ms = now + CONFIG_APP_REPORT_INTERVAL_MS;

	if (len < header || len > sizeof(res)) {
		LOG_INF("result %u: %u bytes", seq, (uint32_t)len);
		return;
	}

	memcpy(&res, payload, len);
	if (res.channels > RESULT_MAX_CHANNELS ||
	    len != header + res.channels * sizeof(res.ch[0])) {
		LOG_INF("result %u: %u bytes, not a batch_summary", seq, (uint32_t)len);
		return;
	}

	for (uint32_t c = 0; c < res.channels; c++) {
		LOG_INF("result %u ch%u: min %d max %d mean %d, n %u", seq, c,
			res.ch[c].min, res.ch[c].max, res.ch[c].mean, res.samples);
	}
}
