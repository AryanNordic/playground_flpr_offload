/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#ifndef APP_METRICS_H_
#define APP_METRICS_H_

#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>

/*
 * Small measurement helpers shared by both images.
 *
 * The activity marker drives a GPIO high while the workload acquires and
 * processes a batch, so an external power analyser can line up current draw with
 * compute. It is optional:
 * if the board has no "offload-marker" alias the calls compile to nothing and
 * the sample still runs and still reports the M33 load.
 */

#define OFFLOAD_MARKER_NODE DT_ALIAS(offload_marker)

#if DT_NODE_EXISTS(OFFLOAD_MARKER_NODE)
static const struct gpio_dt_spec offload_marker = GPIO_DT_SPEC_GET(OFFLOAD_MARKER_NODE, gpios);

static inline void marker_init(void)
{
	if (gpio_is_ready_dt(&offload_marker)) {
		(void)gpio_pin_configure_dt(&offload_marker, GPIO_OUTPUT_INACTIVE);
	}
}

static inline void marker_set(int on)
{
	(void)gpio_pin_set_dt(&offload_marker, on);
}
#else
static inline void marker_init(void)
{
}

static inline void marker_set(int on)
{
	ARG_UNUSED(on);
}
#endif

static inline uint32_t cycles_to_us(uint32_t cycles)
{
	return (uint32_t)(((uint64_t)cycles * 1000000U) / sys_clock_hw_cycles_per_sec());
}

#ifdef CONFIG_SCHED_THREAD_USAGE_ALL
/*
 * Rolling CPU load. Sample it once per report interval; each call returns the
 * active share of the CPU over the elapsed interval, in per-mille (0..1000).
 * This is the number offloading changes. Time is attributed at context switches,
 * so an interrupt that arrives while the CPU is idle counts as idle time.
 */
struct cpu_load {
	uint64_t exec;
	uint64_t idle;
};

static inline uint32_t cpu_load_active_permille(struct cpu_load *s)
{
	k_thread_runtime_stats_t rt;

	(void)k_thread_runtime_stats_all_get(&rt);

	uint64_t d_exec = rt.execution_cycles - s->exec;
	uint64_t d_idle = rt.idle_cycles - s->idle;

	s->exec = rt.execution_cycles;
	s->idle = rt.idle_cycles;

	if (d_exec == 0) {
		return 0;
	}

	return (uint32_t)(((d_exec - d_idle) * 1000U) / d_exec);
}
#endif /* CONFIG_SCHED_THREAD_USAGE_ALL */

#endif /* APP_METRICS_H_ */
