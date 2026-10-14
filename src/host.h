/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#ifndef HOST_H_
#define HOST_H_

#include <stdint.h>

/*
 * The M33 host. main.c prints the report; one of two files runs the workload:
 * offloaded.c (the FLPR runs it, the M33 receives the results) or local.c (the
 * M33 runs it itself, the baseline). CONFIG_APP_OFFLOAD picks the file.
 */

/* Runs the whole sample. Returns when the run ends or fails. */
int host_run(void);

/* The once-per-interval report line, implemented in main.c. */
void report_start(void);
void report_batch(uint32_t acquire_us, uint32_t process_us);
void report_print(void);

#endif /* HOST_H_ */
