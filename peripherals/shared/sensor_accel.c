/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

/*
 * Accelerometer backend on the Zephyr sensor API, for both triggers.
 *
 * It is shared by peripherals/twim_lis2dh (LIS2DH12 on TWIM) and
 * peripherals/spim_adxl362 (ADXL362 on SPIM). Nothing in it depends on the sensor
 * model or on the bus: the folder's peripheral.overlay picks the device and its
 * conf fragment picks the driver. This is the same sensor API code an
 * application already has on the M33, and it runs unchanged on the FLPR.
 *
 * The raw batch is int16_t x, y, z triplets in cm/s^2 (integer math only; the
 * FLPR has no FPU). The timer trigger reads one triplet per batch. The
 * interrupt trigger collects SAMPLES_PER_BATCH triplets, one per data-ready
 * interrupt at the sensor's output data rate, and wakes the loop once per batch.
 */

#include <string.h>

#include <zephyr/device.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "workload.h"

LOG_MODULE_REGISTER(workload, LOG_LEVEL_INF);

BUILD_ASSERT(CONFIG_APP_RAW_CHANNELS == 3, "the raw batch is x, y, z triplets");

static const struct device *const accel = DEVICE_DT_GET(DT_ALIAS(offload_peripheral));

/* Read one sample and store x, y, z in cm/s^2. */
static int accel_read_xyz(int16_t xyz[3])
{
	struct sensor_value v[3];
	int err;

	err = sensor_sample_fetch(accel);
	if (err == 0) {
		err = sensor_channel_get(accel, SENSOR_CHAN_ACCEL_XYZ, v);
	}
	if (err < 0) {
		LOG_WRN_ONCE("%s read failed (%d), check the wiring", accel->name, err);
		return err;
	}

	for (int i = 0; i < 3; i++) {
		/* val2 is in millionths and carries the sign of val1. */
		xyz[i] = (int16_t)(v[i].val1 * 100 + v[i].val2 / 10000);
	}

	return 0;
}

static int accel_ready(void)
{
	if (!device_is_ready(accel)) {
		LOG_ERR("%s not ready", accel->name);
		return -ENODEV;
	}

	return 0;
}

#if defined(CONFIG_APP_TRIGGER_INTERRUPT)

/* Samples per batch: 160 ms of data at the 100 Hz rate set in irq.conf. */
#define SAMPLES_PER_BATCH 16

struct accel_batch {
	int16_t xyz[SAMPLES_PER_BATCH][3];
};

BUILD_ASSERT(sizeof(struct accel_batch) <= WORKLOAD_RAW_MAX,
	     "accelerometer batch does not fit in WORKLOAD_RAW_MAX");

/*
 * Full batches, passed by value from the driver's trigger thread to the loop
 * thread, so neither side ever reads a buffer the other is writing. If the loop
 * falls two batches behind, new batches are dropped.
 */
K_MSGQ_DEFINE(batch_q, sizeof(struct accel_batch), 2, sizeof(uint32_t));

static struct accel_batch filling; /* driver trigger thread only */
static size_t filled;              /* driver trigger thread only */
static struct accel_batch ready;   /* loop thread only */

/* Static: the driver keeps this pointer. */
static const struct sensor_trigger drdy_trig = {
	.type = SENSOR_TRIG_DATA_READY,
	.chan = SENSOR_CHAN_ACCEL_XYZ,
};

/*
 * Runs in the driver's trigger thread, once per new sample. It has to read the
 * sample: on both the LIS2DH and the ADXL362, reading the data is what clears
 * data-ready, and until it is cleared the next data-ready interrupt does not come.
 */
static void drdy_handler(const struct device *dev, const struct sensor_trigger *trig)
{
	ARG_UNUSED(dev);
	ARG_UNUSED(trig);

	if (accel_read_xyz(filling.xyz[filled]) < 0) {
		return;
	}

	if (++filled < SAMPLES_PER_BATCH) {
		return;
	}

	filled = 0;
	if (k_msgq_put(&batch_q, &filling, K_NO_WAIT) < 0) {
		LOG_WRN("batch dropped, the loop is behind");
	}
}

int workload_init(void)
{
	int err = accel_ready();

	if (err < 0) {
		return err;
	}

	err = sensor_trigger_set(accel, &drdy_trig, drdy_handler);
	if (err < 0) {
		LOG_ERR("data-ready trigger setup failed (%d)", err);
	}

	return err;
}

int workload_wait(void)
{
	return k_msgq_get(&batch_q, &ready, K_FOREVER);
}

size_t workload_acquire(void *raw)
{
	memcpy(raw, &ready, sizeof(ready));
	return sizeof(ready);
}

void workload_stop(void)
{
	/* A NULL handler turns data-ready off in both drivers. */
	(void)sensor_trigger_set(accel, &drdy_trig, NULL);
}

#else /* CONFIG_APP_TRIGGER_INTERRUPT */

int workload_init(void)
{
	return accel_ready();
}

size_t workload_acquire(void *raw)
{
	int16_t *xyz = raw;

	if (accel_read_xyz(xyz) < 0) {
		return 0;
	}

	return 3 * sizeof(int16_t);
}

#endif /* CONFIG_APP_TRIGGER_INTERRUPT */
