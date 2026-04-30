// Copyright 2026 ETH Zurich, University of Bologna and Fondazione Chips-IT.
// Licensed under the Apache License, Version 2.0, see LICENSE for details.
// SPDX-License-Identifier: Apache-2.0

#include "pulp.h"
#include <hal/mailboxes/mailboxes.h>

// Fixed-iteration busy loop: must be identical across all rounds so ibex
// measures only the clock divider effect.
#define BUSY_LOOP_ITERS  100000

int main(void) {

    if (get_core_id() == 0) {
        for (volatile int i = 0; i < BUSY_LOOP_ITERS; i++);
    }

    synch_barrier();
    return 0;
}
