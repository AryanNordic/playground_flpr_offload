/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

/*
 * Offloaded build: the FLPR runs the workload and the M33 only forwards.
 *
 * The M33 starts the FLPR with one control message. After that each result
 * arrives over IPC and is handed to app_transmit() from this thread. The IPC
 * receive callback runs in the IPC service work queue, which must not block, so
 * it only copies the message into a queue.
 */

#include <zephyr/kernel.h>
#include <zephyr/ipc/ipc_service.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/util.h>

#include "host.h"
#include "offload_protocol.h"
#include "workload.h"

LOG_MODULE_DECLARE(host);

/* Results waiting for app_transmit(). When it falls this far behind, results are dropped. */
#define RESULT_QUEUE_DEPTH 8

static const struct device *const ipc0 = DEVICE_DT_GET(DT_NODELABEL(ipc0));
static struct ipc_ept ep;
static K_SEM_DEFINE(bound_sem, 0, 1);
K_MSGQ_DEFINE(result_q, sizeof(struct offload_msg), RESULT_QUEUE_DEPTH, 4);

static atomic_t dropped;
static atomic_t run_ended;
static atomic_t run_error;

static void ep_bound(void *priv)
{
	ARG_UNUSED(priv);
	k_sem_give(&bound_sem);
}

/* IPC service work queue: copy and return. */
static void ep_recv(const void *data, size_t len, void *priv)
{
	const struct offload_msg *msg = data;

	ARG_UNUSED(priv);

	if (len != sizeof(*msg)) {
		LOG_WRN_ONCE("unexpected message size %u, are both images from the same build?",
			     (uint32_t)len);
		return;
	}

	switch (msg->type) {
	case OFFLOAD_MSG_RESULT:
		if (k_msgq_put(&result_q, msg, K_NO_WAIT) < 0) {
			atomic_inc(&dropped);
		}
		break;
	case OFFLOAD_MSG_STATUS:
		if (msg->status == OFFLOAD_STATUS_DONE) {
			atomic_set(&run_ended, 1);
		}
		break;
	case OFFLOAD_MSG_ERROR:
		atomic_set(&run_error, msg->error);
		atomic_set(&run_ended, 1);
		break;
	default:
		break;
	}
}

static int send_control(void)
{
	const struct offload_msg msg = {
		.type = OFFLOAD_MSG_CONTROL,
		.control = {
			.iterations = CONFIG_APP_ITERATIONS,
			.sample_interval_ms = CONFIG_APP_SAMPLE_INTERVAL_MS,
		},
	};

	/* -ENOMEM means the shared buffer is momentarily full; try again shortly. */
	for (int try = 0; try < 100; try++) {
		int ret = ipc_service_send(&ep, &msg, sizeof(msg));

		if (ret >= 0) {
			return 0;
		}
		if (ret != -ENOMEM) {
			return ret;
		}
		k_msleep(1);
	}

	return -ETIMEDOUT;
}

static void forward(const struct offload_msg *msg)
{
	const struct offload_result *res = &msg->result;

	report_batch(res->acquire_us, res->process_us);
	app_transmit(res->iteration, res->bytes, MIN(res->len, sizeof(res->bytes)));
}

int host_run(void)
{
	static struct ipc_ept_cfg ep_cfg = {
		.name = "offload",
		.cb = {
			.bound = ep_bound,
			.received = ep_recv,
		},
	};
	struct offload_msg msg;
	uint32_t prev_dropped = 0;
	int64_t next_report;
	int ret;

	ret = ipc_service_open_instance(ipc0);
	if (ret < 0 && ret != -EALREADY) {
		LOG_ERR("ipc_service_open_instance failed (%d)", ret);
		return ret;
	}

	ret = ipc_service_register_endpoint(ipc0, &ep, &ep_cfg);
	if (ret < 0) {
		LOG_ERR("ipc_service_register_endpoint failed (%d)", ret);
		return ret;
	}

	while (k_sem_take(&bound_sem, K_SECONDS(5)) != 0) {
		LOG_WRN("no answer from the FLPR yet: was the FLPR image flashed? "
			"(west flash from the build directory flashes both)");
	}

	ret = send_control();
	if (ret < 0) {
		LOG_ERR("failed to start the FLPR (%d)", ret);
		return ret;
	}
	LOG_INF("FLPR started");

	report_start();
	next_report = k_uptime_get() + CONFIG_APP_REPORT_INTERVAL_MS;

	while (!atomic_get(&run_ended)) {
		int64_t wait_ms = MAX(next_report - k_uptime_get(), 0);

		if (k_msgq_get(&result_q, &msg, K_MSEC(wait_ms)) == 0) {
			forward(&msg);
		}

		if (k_uptime_get() >= next_report) {
			uint32_t d = (uint32_t)atomic_get(&dropped);

			report_print();
			if (d != prev_dropped) {
				LOG_WRN("%u results dropped: app_transmit() is slower than the FLPR",
					d - prev_dropped);
				prev_dropped = d;
			}
			/* From now, not from the last deadline: a long app_transmit() must not
			 * cause a burst of reports.
			 */
			next_report = k_uptime_get() + CONFIG_APP_REPORT_INTERVAL_MS;
		}
	}

	/* The FLPR sends its last results before DONE or ERROR, so they are queued. */
	while (k_msgq_get(&result_q, &msg, K_NO_WAIT) == 0) {
		forward(&msg);
	}

	ret = (int)atomic_get(&run_error);
	if (ret < 0) {
		LOG_ERR("the FLPR stopped the workload with error %d; its own log is on the "
			"FLPR console (uart30)", ret);
		return ret;
	}

	LOG_INF("run complete");
	return 0;
}
