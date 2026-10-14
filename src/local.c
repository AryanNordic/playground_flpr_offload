/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

/*
 * Baseline build: the M33 runs the same workload itself, from the same source
 * the FLPR runs in the offloaded build. Compare its report line with the
 * offloaded one.
 *
 * The workload runs in its own thread at the lowest application priority, the
 * priority of the log thread, so that even back to back (interval 0) it shares
 * the CPU with logging through time slicing. The report runs in main, which
 * preempts it once per interval.
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "host.h"
#include "workload.h"
#include "workload_loop.h"

LOG_MODULE_DECLARE(host);

#define WORKLOAD_STACK_SIZE 2048

static K_THREAD_STACK_DEFINE(workload_stack, WORKLOAD_STACK_SIZE);
static struct k_thread workload_thread;
static int workload_ret;

static int emit(uint32_t seq, const void *result, size_t len, uint32_t acquire_us,
		uint32_t process_us)
{
	report_batch(acquire_us, process_us);
	app_transmit(seq, result, len);
	return 0;
}

static void workload_entry(void *p1, void *p2, void *p3)
{
	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	workload_ret = workload_loop(CONFIG_APP_ITERATIONS, CONFIG_APP_SAMPLE_INTERVAL_MS, emit);
}

int host_run(void)
{
	int ret = workload_init();

	if (ret < 0) {
		LOG_ERR("workload_init failed (%d)", ret);
		return ret;
	}

	report_start();
	k_thread_create(&workload_thread, workload_stack, K_THREAD_STACK_SIZEOF(workload_stack),
			workload_entry, NULL, NULL, NULL, K_LOWEST_APPLICATION_THREAD_PRIO, 0,
			K_NO_WAIT);
	k_thread_name_set(&workload_thread, "workload");

	/* Print the report every interval until the workload thread ends. */
	while (k_thread_join(&workload_thread, K_MSEC(CONFIG_APP_REPORT_INTERVAL_MS)) != 0) {
		report_print();
	}

	if (workload_ret < 0) {
		LOG_ERR("workload stopped (%d)", workload_ret);
		return workload_ret;
	}

	LOG_INF("run complete");
	return 0;
}
