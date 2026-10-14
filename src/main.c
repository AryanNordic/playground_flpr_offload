/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

/*
 * M33 entry point and the measurement report. Nothing here changes with the
 * peripheral or the processing; your M33 code is app/app_transmit.c.
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/atomic.h>

#include "app_metrics.h"
#include "host.h"

LOG_MODULE_REGISTER(host, LOG_LEVEL_INF);

#define WORKLOAD_CORE (IS_ENABLED(CONFIG_APP_OFFLOAD) ? "FLPR" : "M33")

/* Written per result by report_batch(), read once per interval by report_print(). */
static atomic_t results;
static atomic_t acquire_sum_us;
static atomic_t process_sum_us;

static struct cpu_load load;
static int64_t prev_ms;
static uint32_t prev_results;
static uint32_t prev_acquire_us;
static uint32_t prev_process_us;

void report_start(void)
{
	(void)cpu_load_active_permille(&load);
	prev_ms = k_uptime_get();
}

void report_batch(uint32_t acquire_us, uint32_t process_us)
{
	atomic_add(&acquire_sum_us, (atomic_val_t)acquire_us);
	atomic_add(&process_sum_us, (atomic_val_t)process_us);
	atomic_inc(&results);
}

/*
 * One line per interval: the M33 load, the result rate, and the average time the
 * workload core spent acquiring and processing one batch. Unsigned differences
 * keep the counters correct across wraparound.
 */
void report_print(void)
{
	int64_t now = k_uptime_get();
	uint32_t dt_ms = (uint32_t)(now - prev_ms);
	uint32_t n = (uint32_t)atomic_get(&results);
	uint32_t acq = (uint32_t)atomic_get(&acquire_sum_us);
	uint32_t proc = (uint32_t)atomic_get(&process_sum_us);
	uint32_t d_n = n - prev_results;
	uint32_t d_acq = acq - prev_acquire_us;
	uint32_t d_proc = proc - prev_process_us;
	uint32_t active = cpu_load_active_permille(&load);
	uint32_t rate10 = dt_ms ? (uint32_t)((uint64_t)d_n * 10000U / dt_ms) : 0;

	prev_ms = now;
	prev_results = n;
	prev_acquire_us = acq;
	prev_process_us = proc;

	if (d_n == 0) {
		LOG_INF("M33 active %u.%u%% | 0 results/s", active / 10, active % 10);
		return;
	}

	uint32_t acq10 = (uint32_t)((uint64_t)d_acq * 10U / d_n);
	uint32_t proc10 = (uint32_t)((uint64_t)d_proc * 10U / d_n);

	LOG_INF("M33 active %u.%u%% | %u.%u results/s | %s per batch: acquire %u.%u us, "
		"process %u.%u us",
		active / 10, active % 10, rate10 / 10, rate10 % 10, WORKLOAD_CORE,
		acq10 / 10, acq10 % 10, proc10 / 10, proc10 % 10);
}

int main(void)
{
	LOG_INF("FLPR offload sample: workload on %s, peripheral %s, %s trigger",
		WORKLOAD_CORE, CONFIG_APP_PERIPHERAL,
		IS_ENABLED(CONFIG_APP_TRIGGER_INTERRUPT) ? "interrupt" : "timer");
	marker_init();

	return host_run();
}
