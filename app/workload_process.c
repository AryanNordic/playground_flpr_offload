/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

/*
 * Your processing: workload_process().
 *
 * It turns one raw batch into the result sent to the M33. It is one of the three
 * workload functions; the other two, workload_init() and workload_acquire(), are
 * in the peripheral folder you build with (peripherals/<name>/). In the offloaded
 * build all three run on the FLPR, in the baseline build all three run on the
 * M33, from this same source.
 *
 *   raw      the batch workload_acquire() just read
 *   raw_len  its size in bytes
 *   out      where the result goes, at most WORKLOAD_RESULT_MAX bytes
 *   return   the number of bytes written to out; 0 sends nothing for this batch
 *
 * Keep it integer or fixed point. The FLPR has no FPU, so floating point is
 * emulated in software there and can be slower than on the M33.
 *
 * If your result has a different layout, change app/result.h and the decoding
 * in app/app_transmit.c to match.
 */

#include <stddef.h>
#include <string.h>

#include <zephyr/toolchain.h>

#include "result.h"
#include "workload.h"

BUILD_ASSERT(CONFIG_APP_RAW_CHANNELS <= RESULT_MAX_CHANNELS,
	     "the example result holds at most RESULT_MAX_CHANNELS channels");

size_t workload_process(const void *raw, size_t raw_len, void *out)
{
	/*
	 * EXAMPLE, replace the body with your processing.
	 *
	 * The raw batch is int16_t samples, CONFIG_APP_RAW_CHANNELS of them
	 * interleaved (1 for the SAADC, 3 for the x, y, z of an accelerometer; the
	 * peripheral's conf files set it). Each channel is reduced to its minimum,
	 * maximum and mean.
	 */
	const int16_t *samples = raw;
	const size_t channels = CONFIG_APP_RAW_CHANNELS;
	const size_t per_channel = raw_len / sizeof(int16_t) / channels;
	struct batch_summary res = {
		.samples = (uint16_t)per_channel,
		.channels = (uint16_t)channels,
	};
	size_t len;

	if (per_channel == 0) {
		/* Fewer samples than channels, a cut-short read; send nothing. */
		return 0;
	}

	for (size_t c = 0; c < channels; c++) {
		int32_t sum = 0;
		int16_t min = INT16_MAX;
		int16_t max = INT16_MIN;

		for (size_t i = 0; i < per_channel; i++) {
			int16_t s = samples[i * channels + c];

			if (s < min) {
				min = s;
			}
			if (s > max) {
				max = s;
			}
			sum += s;
		}

		res.ch[c].min = min;
		res.ch[c].max = max;
		res.ch[c].mean = (int16_t)(sum / (int32_t)per_channel);
	}

	/* Send only the channels in use. */
	len = offsetof(struct batch_summary, ch) + channels * sizeof(res.ch[0]);
	memcpy(out, &res, len);
	return len;
}
