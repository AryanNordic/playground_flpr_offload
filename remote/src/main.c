/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

/*
 * FLPR image. It holds only the transport to the M33: the workload itself (the
 * peripheral backend, the batch loop and the processing) is the same code the M33
 * runs in the local baseline, selected at build time by -DOFFLOAD_PERIPHERAL and
 * -DOFFLOAD_TRIGGER. Nothing in this file changes with the peripheral or the
 * trigger.
 */

#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/ipc/ipc_service.h>
#include <zephyr/logging/log.h>

#include "offload_protocol.h"
#include "workload.h"
#include "workload_loop.h"
#include "app_metrics.h"

LOG_MODULE_REGISTER(flpr, LOG_LEVEL_INF);

static const struct device *const ipc0 = DEVICE_DT_GET(DT_NODELABEL(ipc0));
static struct ipc_ept ep;
static K_SEM_DEFINE(bound_sem, 0, 1);
static K_SEM_DEFINE(control_sem, 0, 1);

static struct offload_control control;

static void ep_bound(void *priv)
{
	k_sem_give(&bound_sem);
}

static void ep_recv(const void *data, size_t len, void *priv)
{
	const struct offload_msg *msg = data;

	if (len != sizeof(*msg)) {
		return;
	}

	if (msg->type == OFFLOAD_MSG_CONTROL) {
		control = msg->control;
		k_sem_give(&control_sem);
	}
}

static int send_msg(const struct offload_msg *msg)
{
	while (true) {
		int ret = ipc_service_send(&ep, msg, sizeof(*msg));

		if (ret >= 0) {
			return 0;
		}
		if (ret != -ENOMEM) {
			return ret;
		}
		/* The M33 has not drained the shared buffer yet. Sleep rather than spin,
		 * so lower-priority threads (logging) still run.
		 */
		k_msleep(1);
	}
}

static int send_status(uint32_t status)
{
	const struct offload_msg msg = {
		.type = OFFLOAD_MSG_STATUS,
		.status = status,
	};

	return send_msg(&msg);
}

static void send_error(int error)
{
	const struct offload_msg msg = {
		.type = OFFLOAD_MSG_ERROR,
		.error = error,
	};

	(void)send_msg(&msg);
}

/* The loop hands each result here; it goes to the M33 as one RESULT message. */
static int emit(uint32_t seq, const void *result, size_t len, uint32_t acquire_us,
		uint32_t process_us)
{
	struct offload_msg msg = {
		.type = OFFLOAD_MSG_RESULT,
		.result = {
			.iteration = seq,
			.acquire_us = acquire_us,
			.process_us = process_us,
			.len = (uint16_t)len,
		},
	};

	memcpy(msg.result.bytes, result, len);
	return send_msg(&msg);
}

int main(void)
{
	static struct ipc_ept_cfg ep_cfg = {
		.name = "offload",
		.cb = {
			.bound = ep_bound,
			.received = ep_recv,
		},
	};
	int ret;

	marker_init();

	ret = ipc_service_open_instance(ipc0);
	if (ret < 0 && ret != -EALREADY) {
		return ret;
	}

	ret = ipc_service_register_endpoint(ipc0, &ep, &ep_cfg);
	if (ret < 0) {
		return ret;
	}

	k_sem_take(&bound_sem, K_FOREVER);
	LOG_INF("endpoint bound, waiting for control");

	k_sem_take(&control_sem, K_FOREVER);

	ret = workload_init();
	if (ret < 0) {
		send_error(ret);
		return ret;
	}

	LOG_INF("workload %s, %s trigger", CONFIG_APP_PERIPHERAL,
		IS_ENABLED(CONFIG_APP_TRIGGER_INTERRUPT) ? "interrupt" : "timer");
	(void)send_status(OFFLOAD_STATUS_RUNNING);

	ret = workload_loop(control.iterations, control.sample_interval_ms, emit);
	if (ret < 0) {
		send_error(ret);
		return ret;
	}

	(void)send_status(OFFLOAD_STATUS_DONE);
	LOG_INF("run complete");
	return 0;
}
