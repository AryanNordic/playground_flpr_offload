/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#ifndef APP_WORKLOAD_H_
#define APP_WORKLOAD_H_

#include <stddef.h>
#include <stdint.h>

/*
 * Everything you implement, in one place.
 *
 * The workload is three functions. In the offloaded build all three run on the
 * FLPR; in the baseline build all three run on the M33. You write them once:
 *
 *   workload_init()     set up the peripheral, once before the first batch
 *   workload_acquire()  read one raw batch
 *   workload_process()  turn the batch into a result
 *
 * init and acquire live in the peripheral folder you build with
 * (peripherals/<name>/), process lives in app/workload_process.c. The interrupt
 * trigger adds workload_wait() and workload_stop(), also in the peripheral
 * folder.
 *
 * Each result then reaches app_transmit() on the M33 (app/app_transmit.c), in
 * both builds. That is where your radio send goes.
 *
 * Moving all three workload functions to the FLPR is how this template is wired,
 * not a hardware limit: the IPC works both ways, so raw batches could also be
 * sent from the M33 to the FLPR. This template does not implement that path.
 *
 * The result is opaque bytes to everything between workload_process() and
 * app_transmit(). Only those two need to agree on its layout (app/result.h for
 * the example).
 */

/* Upper bound on one raw batch from workload_acquire(), in bytes. */
#define WORKLOAD_RAW_MAX 320

/*
 * Upper bound on one result from workload_process(), in bytes. It also sizes the
 * IPC message that carries the result, so both images must be rebuilt together
 * when it changes.
 */
#define WORKLOAD_RESULT_MAX 32

/*
 * One-time setup on the core that runs the workload, before the first batch.
 * Return 0, or a negative errno. On the FLPR the error is reported to the M33,
 * which logs it.
 */
int workload_init(void);

/*
 * Read one raw batch into "raw" (at most WORKLOAD_RAW_MAX bytes, 4-byte aligned)
 * and return the number of bytes written, or 0 if the read failed. With the
 * timer trigger this is the peripheral read itself. With the interrupt trigger
 * the peripheral already collected the batch, and this copies it out.
 */
size_t workload_acquire(void *raw);

/*
 * Turn the raw batch ("raw", raw_len bytes, never 0) into a result in "out" (at most
 * WORKLOAD_RESULT_MAX bytes). Return the number of bytes written. Return 0 to
 * send nothing for this batch, for example when there is nothing worth
 * reporting; the M33 is then not involved at all.
 */
size_t workload_process(const void *raw, size_t raw_len, void *out);

/*
 * Interrupt trigger only. Block until the peripheral has a full batch for
 * workload_acquire(); the core sleeps here. Return 0, or a negative errno to end
 * the run.
 */
int workload_wait(void);

/*
 * Interrupt trigger only. Stop the peripheral at the end of a run so it no
 * longer wakes the core.
 */
void workload_stop(void);

/*
 * M33 only, both builds. Called once per result, in a normal thread, so it may
 * block (for example on a radio send). "seq" is the batch index.
 */
void app_transmit(uint32_t seq, const void *payload, size_t len);

#endif /* APP_WORKLOAD_H_ */
