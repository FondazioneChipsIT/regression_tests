/*
 * Copyright (C) 2026 ETH Zurich, University of Bologna and Fondazione Chips-IT
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

/*
 * mbox_wfe_test / PULP-cluster side
 *
 * Mailbox test: the cluster core 0 configures the event unit (EU) and
 * executes a p.elw (Wait-For-Event) instruction.  Ibex (the host) then writes
 * data into the two mailbox letter registers and asserts the RCV interrupt line.
 * The RCV interrupt is mapped to EU event bit 22 of every cluster PE via the
 * cluster_event_map module:
 *
 *   cluster_event_map.sv:
 *     assign events_mapped_o[I][23:22] = cluster_events_i[I][1:0];
 *   cluster_peripherals.sv:
 *     assign s_cluster_events[I][0] = mbox_irq_i;   // bit 22
 *
 * Synchronisation protocol
 * ========================
 * 1. Core 0 enables the mailbox RCV interrupt path (writes IRQ_RCV_EN = 1).
 *    This register is polled by ibex as a "cluster ready" flag.
 * 2. Core 0 adds EU event bit EU_MBOX_EVT_BIT (22) to its event mask and
 *    executes eu_evt_waitAndClr() (p.elw on RI5CY/CV32E40P) -> hardware sleep.
 * 3. Ibex detects that IRQ_RCV_EN is set, writes the two letter registers,
 *    then writes IRQ_RCV_SET = 1.  The mailbox asserts mbox_irq_i -> EU bit 22.
 * 4. Core 0 wakes, clears the RCV interrupt, reads and verifies the letters.
 * 5. All cores synchronise via synch_barrier() and the cluster finalises.
 *
 * Expected letter values written by ibex:
 *   LETTER0 = 0xBAADC0DE
 *   LETTER1 = 0xDEADBEEF
 */

#include <stdio.h>
#include <stdlib.h>
#include "pulp.h"

/*
 * EU event bit for the mailbox receive interrupt.
 * Source: cluster_event_map.sv
 *   assign events_mapped_o[I][22] = cluster_events_i[I][0]  // = mbox_irq_i
 */
#define EU_MBOX_EVT_BIT   22

/* Expected values written by ibex into the mailbox letter registers */
#define EXPECTED_LETTER0  0xBAADC0DE
#define EXPECTED_LETTER1  0xDEADBEEF

int main(void)
{
    int errors = 0;

    if (rt_core_id() == 0) {
        printf("MBOX-WFE TEST [cluster]: starting on core 0\n");

        /*
         * Step 1 - Enable the mailbox RCV interrupt path at the mailbox IP.
         *
         * Writing IRQ_RCV_EN = 1 causes the HW to forward any subsequent
         * assertion of IRQ_RCV_SET (written by ibex) as mbox_irq_i -> EU bit 22.
         * Ibex polls this same register (from its own AXI port) to detect that
         * the cluster is ready before it fires the trigger.
         */
        hal_write_to_mailbox(ARCHI_MAILBOX_IRQ_RCV_EN_OFFSET, 0x1);

        /*
         * Step 2 - Wait-For-Event: add EU event bit 22 to this core's event
         * mask and execute p.elw.  The core goes to hardware clock-gated sleep.
         *
         * eu_evt_maskWaitAndClr() atomically:
         *   a) ORs evtMask into EU_CORE_MASK (adds bit 22 on top of the
         *      dispatch/mutex/barrier bits already set by cluster_core_init)
         *   b) Reads EU_CORE_EVENT_WAIT_CLEAR (p.elw) - sleeps until any
         *      masked event fires
         *   c) ANDs evtMask out of EU_CORE_MASK (removes bit 22)
         *
         * If the event is already buffered when this call executes (e.g. ibex
         * was very fast), p.elw returns immediately - this is correct by the
         * EU specification.
         */
        printf("MBOX-WFE TEST [cluster]: entering WFE (EU bit %d)\n",
               EU_MBOX_EVT_BIT);

        eu_evt_maskWaitAndClr(1u << EU_MBOX_EVT_BIT);

        printf("MBOX-WFE TEST [cluster]: woken by EU event\n");

        /*
         * Step 3 - Clear the RCV interrupt so that mbox_irq_i de-asserts.
         * Write IRQ_RCV_CLR = 1 first (clears the pending flag), then
         * disable the enable bit to fully deassert rcv_irq_o.
         */
        hal_write_to_mailbox(ARCHI_MAILBOX_IRQ_RCV_CLR_OFFSET, 0x1);
        hal_write_to_mailbox(ARCHI_MAILBOX_IRQ_RCV_EN_OFFSET,  0x0);

        /*
         * Step 4 - Read back the two letter registers written by ibex and
         * verify their values.
         */
        uint32_t letter0 = pulp_read32(ARCHI_SOC_MAILBOXES_ADDR +
                                       ARCHI_SOC_MAILBOX_OFFSET  +
                                       ARCHI_MAILBOX_LETTER0_OFFSET);
        uint32_t letter1 = pulp_read32(ARCHI_SOC_MAILBOXES_ADDR +
                                       ARCHI_SOC_MAILBOX_OFFSET  +
                                       ARCHI_MAILBOX_LETTER1_OFFSET);

        printf("MBOX-WFE TEST [cluster]: letter0=0x%08x  letter1=0x%08x\n",
               letter0, letter1);

        if (letter0 != EXPECTED_LETTER0) {
            printf("MBOX-WFE TEST [cluster]: FAIL - letter0 expected 0x%08x"
                   " got 0x%08x\n", EXPECTED_LETTER0, letter0);
            errors++;
        }
        if (letter1 != EXPECTED_LETTER1) {
            printf("MBOX-WFE TEST [cluster]: FAIL - letter1 expected 0x%08x"
                   " got 0x%08x\n", EXPECTED_LETTER1, letter1);
            errors++;
        }

        if (errors == 0)
            printf("MBOX-WFE TEST [cluster]: PASS\n");
    }

    /*
     * Step 5 - All cluster cores synchronise before returning.
     * Cores 1..N-1 blocked here the whole time waiting for core 0 to
     * trigger the hardware barrier (which core 0 does only after waking).
     */
    synch_barrier();

    return errors;
}
