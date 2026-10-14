/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

/*
 * SAADC backend, timer trigger: workload_init() and workload_acquire() reading a
 * batch of samples through the ordinary Zephyr ADC driver.
 *
 * This is the peripheral-specific part of the workload. The loop
 * (common/workload_loop.c) and the processing (app/workload_process.c) do not
 * change with the peripheral: they only speak the common/workload.h contract. The
 * devicetree for the channel is in peripheral.overlay next to this file.
 *
 * These functions run wherever the workload runs: on the FLPR in the offloaded
 * build, on the M33 in the local baseline. The interrupt trigger uses
 * saadc_irq.c instead, which drives the SAADC through nrfx so the peripheral
 * samples on its own and wakes the core. The blocking Zephyr adc_read() cannot
 * express that.
 */

#include <zephyr/device.h>
#include <zephyr/drivers/adc.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>

#include "workload.h"

LOG_MODULE_REGISTER(workload, LOG_LEVEL_INF);

/*
 * Samples read per batch, one int16 each. Raise it for a heavier acquire, within
 * WORKLOAD_RAW_MAX bytes.
 */
#define SAMPLES_PER_BATCH 16

BUILD_ASSERT(SAMPLES_PER_BATCH * sizeof(int16_t) <= WORKLOAD_RAW_MAX,
	     "SAADC batch does not fit in WORKLOAD_RAW_MAX");

/*
 * The "offload-peripheral" alias (peripheral.overlay) points at the SAADC node;
 * its first channel node carries the acquisition settings.
 */
#define SENSOR_NODE    DT_ALIAS(offload_peripheral)
#define SENSOR_CHANNEL DT_CHILD(SENSOR_NODE, channel_0)

static const struct device *const sensor = DEVICE_DT_GET(SENSOR_NODE);
static const struct adc_channel_cfg channel_cfg = ADC_CHANNEL_CFG_DT(SENSOR_CHANNEL);

int workload_init(void)
{
	int err;

	if (!device_is_ready(sensor)) {
		LOG_ERR("SAADC device not ready");
		return -ENODEV;
	}

	err = adc_channel_setup(sensor, &channel_cfg);
	if (err < 0) {
		LOG_ERR("SAADC channel setup failed (%d)", err);
		return err;
	}

	return 0;
}

size_t workload_acquire(void *raw)
{
	int16_t *samples = raw;
	int16_t sample = 0;
	struct adc_sequence seq = {
		.channels = BIT(channel_cfg.channel_id),
		.buffer = &sample,
		.buffer_size = sizeof(sample),
		.resolution = DT_PROP(SENSOR_CHANNEL, zephyr_resolution),
	};
	size_t count = 0;

	for (int i = 0; i < SAMPLES_PER_BATCH; i++) {
		int err = adc_read(sensor, &seq);

		if (err < 0) {
			LOG_WRN_ONCE("adc_read failed (%d), the batch is cut short", err);
			break;
		}
		samples[count++] = sample;
	}

	return count * sizeof(int16_t);
}
