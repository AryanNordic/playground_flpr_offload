/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#ifndef APP_WORKLOAD_LOOP_H_
#define APP_WORKLOAD_LOOP_H_

#include <stddef.h>
#include <stdint.h>

/*
 * Receives one result and the time the batch took. On the FLPR it sends the
 * result to the M33; in the baseline build the M33 handles it directly. Return
 * 0 to continue, or a negative errno to stop the run.
 */
typedef int (*workload_emit_t)(uint32_t seq, const void *result, size_t len,
			       uint32_t acquire_us, uint32_t process_us);

/*
 * The batch loop, identical on the M33 and on the FLPR so that the two
 * measurements compare like for like. Call it after workload_init() succeeds.
 *
 * Each batch waits for its trigger, then times workload_acquire() and
 * workload_process() separately and hands the result to "emit". A batch whose
 * process step returns 0 bytes is not emitted. The timer trigger sleeps
 * "interval_ms" between batches (0 runs back to back). The interrupt trigger
 * blocks in workload_wait() and ignores "interval_ms".
 *
 * Runs "iterations" batches, or forever if it is 0. Returns 0 when the run
 * completes, or the first negative errno from the backend or from "emit".
 */
int workload_loop(uint32_t iterations, uint32_t interval_ms, workload_emit_t emit);

#endif /* APP_WORKLOAD_LOOP_H_ */
