/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

/*
 * SAADC backend, interrupt trigger.
 *
 * The SAADC samples on its own, paced by its internal timer, and fills a buffer by
 * EasyDMA with no core involvement. When a buffer is full the SAADC DONE
 * interrupt wakes the core running this code: the handler copies the batch out,
 * workload_wait() returns, the loop processes it, and the core goes back to sleep.
 * Between batches the core draws idle current. Without the interrupt the core
 * would have to poll, which is what saadc_poll.c does.
 *
 * This backend owns the SAADC through nrfx (advanced mode, internal timer), so
 * the Zephyr ADC driver is left out (irq.conf sets CONFIG_ADC=n): it would
 * install its own SAADC interrupt handler and initialise nrfx a second time.
 */

#include <string.h>

#include <zephyr/drivers/adc.h>
#include <zephyr/kernel.h>
#include <zephyr/irq.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>

#include <nrfx_saadc.h>

#include "workload.h"

LOG_MODULE_REGISTER(workload, LOG_LEVEL_INF);

/*
 * The sensor node and its first channel come from the same devicetree the timer
 * backend uses. The pin and the acquisition time are read from it; gain,
 * reference and resolution are fixed below, and a BUILD_ASSERT stops the build if
 * the overlay asks for something else. The Zephyr binding value for
 * "zephyr,input-positive" equals the nrfx analog-input value (a BUILD_ASSERT in
 * the Zephyr SAADC driver guarantees it), so it feeds nrfx directly.
 */
#define SENSOR_NODE    DT_ALIAS(offload_peripheral)
#define SENSOR_CHANNEL DT_CHILD(SENSOR_NODE, channel_0)
#define SENSOR_AIN     DT_PROP(SENSOR_CHANNEL, zephyr_input_positive)

/* Resolution of the conversion; matches the "zephyr,resolution" in the overlay. */
#define SAADC_RESOLUTION NRF_SAADC_RESOLUTION_10BIT

BUILD_ASSERT(DT_PROP(SENSOR_CHANNEL, zephyr_resolution) == 10,
	     "the overlay resolution differs from SAADC_RESOLUTION, change both");
BUILD_ASSERT(DT_ENUM_HAS_VALUE(SENSOR_CHANNEL, zephyr_gain, adc_gain_1),
	     "the overlay gain differs from NRF_SAADC_GAIN1 in workload_init(), change both");
BUILD_ASSERT(DT_ENUM_HAS_VALUE(SENSOR_CHANNEL, zephyr_reference, adc_ref_internal),
	     "the overlay reference differs from the one in workload_init(), change both");

/*
 * Acquisition time from "zephyr,acquisition-time" in the overlay, the same value
 * the Zephyr ADC driver uses in the timer backend. On this SoC the register takes
 * (TACQ + 1) x 125 ns, so it is converted here the way that driver converts it.
 */
#define ACQ_TIME_DT  DT_PROP(SENSOR_CHANNEL, zephyr_acquisition_time)
#define ACQ_TIME_US  ((uint32_t)ADC_ACQ_TIME_VALUE(ACQ_TIME_DT))
#define SAADC_TACQ   (ACQ_TIME_US * 1000U / 125U - 1U)
/* Conversion time, in microseconds (SAADC electrical specification). */
#define CONV_TIME_US 2UL

BUILD_ASSERT(ADC_ACQ_TIME_UNIT(ACQ_TIME_DT) == ADC_ACQ_TIME_MICROSECONDS,
	     "set zephyr,acquisition-time in microseconds");
#if !NRF_SAADC_HAS_ACQTIME_ENUM
BUILD_ASSERT(SAADC_TACQ >= SAADC_CH_CONFIG_TACQ_Min && SAADC_TACQ <= SAADC_CH_CONFIG_TACQ_Max,
	     "zephyr,acquisition-time is out of the SAADC range");
#endif

/*
 * Sample rate driven by the SAADC internal timer. The internal timer runs from a
 * 16 MHz clock; its compare value is that clock divided by the sample rate and
 * must land in the range the hardware allows. The MDK gives the rate as
 * 16 MHz / CC; the Zephyr ADC driver programs CC - 1 for the same rate, which
 * would make this 7996 Hz. Either way it is about 8 kHz.
 */
#define INTERNAL_TIMER_FREQ_HZ 16000000UL
#define SAADC_SAMPLE_FREQ_HZ   8000UL
#define INTERNAL_TIMER_CC      (INTERNAL_TIMER_FREQ_HZ / SAADC_SAMPLE_FREQ_HZ)

/*
 * Samples per DMA buffer. One full buffer is one batch: the SAADC raises DONE
 * once it is filled, which sets the wake rate (SAADC_SAMPLE_FREQ_HZ divided by
 * this, 50 Hz here). Two buffers so the SAADC keeps sampling into one while the
 * handler copies the other out.
 */
#define SAMPLES_PER_BUFFER 160
#define BUFFER_COUNT       2

BUILD_ASSERT(SAADC_SAMPLE_FREQ_HZ <= (1000000UL / (ACQ_TIME_US + CONV_TIME_US)),
	     "SAADC sample rate exceeds what the acquisition time allows");
BUILD_ASSERT(INTERNAL_TIMER_CC >= SAADC_SAMPLERATE_CC_Min &&
		     INTERNAL_TIMER_CC <= SAADC_SAMPLERATE_CC_Max,
	     "SAADC internal-timer compare value is out of the hardware range");
BUILD_ASSERT(SAMPLES_PER_BUFFER * sizeof(nrf_saadc_value_t) <= WORKLOAD_RAW_MAX,
	     "SAADC buffer does not fit in WORKLOAD_RAW_MAX");

static nrf_saadc_value_t sample_buffers[BUFFER_COUNT][SAMPLES_PER_BUFFER];

/*
 * One full batch, copied out of the DMA buffer in the interrupt. The copy is what
 * makes it safe: nrfx hands the just-filled buffer back to the SAADC right away,
 * and the SAADC writes it again one buffer period (20 ms) later, so a loop that
 * falls behind would otherwise read a buffer that is being overwritten.
 */
struct done_batch {
	uint16_t samples;
	nrf_saadc_value_t buf[SAMPLES_PER_BUFFER];
};

K_MSGQ_DEFINE(done_q, sizeof(struct done_batch), 2, sizeof(uint32_t));

/* The batch workload_wait() received, read by workload_acquire(). */
static struct done_batch pending;

static void saadc_handler(nrfx_saadc_evt_t const *evt)
{
	/* buffers[0] is supplied before the start, so the next request wants [1]. */
	static int next = 1;

	switch (evt->type) {
	case NRFX_SAADC_EVT_CALIBRATEDONE:
		/* Calibration finished: begin the continuous, timer-paced sampling. */
		(void)nrfx_saadc_mode_trigger();
		break;

	case NRFX_SAADC_EVT_BUF_REQ:
		/* Keep the conversion continuous by always supplying the next buffer. */
		(void)nrfx_saadc_buffer_set(sample_buffers[next], SAMPLES_PER_BUFFER);
		next ^= 1;
		break;

	case NRFX_SAADC_EVT_DONE: {
		/* A buffer is full: copy it into the queue for the loop thread. */
		static struct done_batch batch;
		uint16_t n = MIN(evt->data.done.size, SAMPLES_PER_BUFFER);

		batch.samples = n;
		memcpy(batch.buf, evt->data.done.p_buffer, n * sizeof(nrf_saadc_value_t));
		if (k_msgq_put(&done_q, &batch, K_NO_WAIT) < 0) {
			LOG_WRN("batch dropped, the loop is behind");
		}
		break;
	}

	default:
		break;
	}
}

int workload_init(void)
{
	nrfx_saadc_channel_t channel = {
		.channel_config = {
			NRFX_COND_CODE_1(NRF_SAADC_HAS_CH_CONFIG_RES,
					 (.resistor_p = NRF_SAADC_RESISTOR_DISABLED,
					  .resistor_n = NRF_SAADC_RESISTOR_DISABLED,),
					 ())
			.gain = NRF_SAADC_GAIN1,
			.reference = NRF_SAADC_REFERENCE_INTERNAL,
			NRFX_COND_CODE_1(NRF_SAADC_HAS_ACQTIME_ENUM,
					 (.acq_time = NRF_SAADC_ACQTIME_10US,),
					 (.acq_time = SAADC_TACQ,))
			.mode = NRF_SAADC_MODE_SINGLE_ENDED,
			NRFX_COND_CODE_1(NRF_SAADC_HAS_CH_BURST,
					 (.burst = NRF_SAADC_BURST_DISABLED,),
					 ())
		},
		.pin_p = (nrfx_analog_input_t)SENSOR_AIN,
		.pin_n = NRFX_ANALOG_INPUT_DISABLED,
		.channel_index = 0,
	};
	nrfx_saadc_adv_config_t adv = NRFX_SAADC_DEFAULT_ADV_CONFIG;
	int err;

	/*
	 * Route the SAADC interrupt to this core. The interrupt line number is the
	 * same on both cores; connecting it here puts the vector on the core that
	 * runs the workload, so DONE wakes this core and not the other one.
	 */
	IRQ_CONNECT(DT_IRQN(SENSOR_NODE), DT_IRQ(SENSOR_NODE, priority),
		    nrfx_isr, nrfx_saadc_irq_handler, 0);

	err = nrfx_saadc_init(DT_IRQ(SENSOR_NODE, priority));
	if (err < 0 && err != -EALREADY) {
		LOG_ERR("nrfx_saadc_init failed (%d)", err);
		return err;
	}

	err = nrfx_saadc_channel_config(&channel);
	if (err < 0) {
		return err;
	}

	adv.internal_timer_cc = INTERNAL_TIMER_CC;
	adv.start_on_end = true;

	err = nrfx_saadc_advanced_mode_set(nrfx_saadc_channels_configured_get(),
					   SAADC_RESOLUTION, &adv, saadc_handler);
	if (err < 0) {
		return err;
	}

	err = nrfx_saadc_buffer_set(sample_buffers[0], SAMPLES_PER_BUFFER);
	if (err < 0) {
		return err;
	}

	/* Calibrate first; the handler starts sampling on CALIBRATEDONE. */
	return nrfx_saadc_offset_calibrate(saadc_handler);
}

int workload_wait(void)
{
	return k_msgq_get(&done_q, &pending, K_FOREVER);
}

size_t workload_acquire(void *raw)
{
	size_t len = (size_t)pending.samples * sizeof(nrf_saadc_value_t);

	memcpy(raw, pending.buf, len);
	return len;
}

void workload_stop(void)
{
	nrfx_saadc_abort();
}
