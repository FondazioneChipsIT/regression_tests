/*
 * Copyright 2026 Fondazione Chips-IT.
 * Licensed under the Apache License, Version 2.0, see LICENSE for details.
 * SPDX-License-Identifier: Apache-2.0
 *
 * snooper_fetch_cluster/test.c — PULP cluster side
 *
 * Overview
 * ========
 * Ibex configures the snooper, boots this binary, then steps back.
 * Core 0 of the cluster takes over the drain loop that was previously
 * performed by Ibex in snooper_fetch_test.c.
 *
 * Key difference vs the Ibex version
 * ------------------------------------
 * The cluster DMA engine issues burst AXI reads to the snooper ring
 * (BASE_SNPR = 0x16000000).  Because snooper entries are 32-bit aligned
 * (5 × 4-byte words = 20 bytes per entry), the DMA can read a full batch
 * in a single descriptor — no per-word CDC crossing overhead like on Ibex.
 *
 * Execution sequence (core 0)
 * ---------------------------
 *   Startup:  allocate L1 staging buffer (snooper entries) and
 *             L1 instruction buffer (fetched basic-block bytes).
 *
 *   Run-1 (halt enabled):
 *     1. Poll L2 ctrl block for SFC_IBEX_RUN1_READY.
 *     2. Drain loop:
 *        a. Poll snooper CFG_REGS_LAST at 0x15000000+0x8.
 *        b. Compute batch (entries before ring wrap, capped at L1_BATCH_ENTRIES).
 *        c. DMA batch: BASE_SNPR + drain_ptr → L1 staging (32-bit burst).
 *        d. For each entry: compute basic-block span, DMA instruction bytes
 *           from CVA6 physical code memory → L1 instruction buffer.
 *        e. Advance drain_ptr; handle wrap at SNPR_RING_BYTES.
 *     3. Loop until CVA6 signals SYNC_FETCH_DONE (scratch 10) AND
 *        snooper LAST == drain_ptr (ring fully drained).
 *     4. Write SFC_CLUSTER_RUN1_DONE to L2 ctrl block.
 *
 *   Run-2 (halt disabled — baseline measurement):
 *     1. Poll L2 ctrl block for SFC_IBEX_RUN2_READY.
 *     2. Same drain loop but without instruction DMA (just consume entries).
 *     3. Loop until CVA6 signals SYNC_FETCH_DONE_RUN2 AND ring fully drained.
 *     4. Write SFC_CLUSTER_RUN2_DONE, ring SND mailbox doorbell.
 *
 * Parallelism
 * -----------
 * Currently only core 0 is active; other cores idle at synch_barriers.
 * Future: broadcast snooper entries to all 8 cores for parallel CFI
 * decode after the batch DMA (see cfi_preprocess as a template).
 *
 * Memory map assumptions
 * ----------------------
 * These addresses are accessible from the cluster via its AXI master port:
 *   0x15000000  snooper config (CFG_REGS_LAST, etc.)
 *   0x16000000  snooper ring buffer (DMA source for drain)
 *   0x03000000  Cheshire host registers (scratch 10 for CVA6 sync state)
 *   CVA6 code   dummy_start … dummy_end (DMA source for instruction fetch)
 */

#include <stdio.h>
#include <stdint.h>
#include "pulp.h"

// ---------------------------------------------------------------------------
// Shared control block definitions (must match snooper_fetch_cluster.h)
// ---------------------------------------------------------------------------
#define SFC_CTRL_BASE         0xA0010000u
#define SFC_IBEX_STATE_OFF    0x00u
#define SFC_DUMMY_START_OFF   0x04u
#define SFC_DUMMY_END_OFF     0x08u
#define SFC_CLUSTER_STATE_OFF 0x0Cu

#define SFC_IDLE              0x00000000u
#define SFC_IBEX_RUN1_READY   0xCAFE0001u
#define SFC_IBEX_RUN2_READY   0xCAFE0002u
#define SFC_CLUSTER_RUN1_DONE 0xDEAD0001u
#define SFC_CLUSTER_RUN2_DONE 0xDEAD0002u

// ---------------------------------------------------------------------------
// Snooper addresses (accessible from cluster via AXI master)
// ---------------------------------------------------------------------------
#define BASE_SNPRCFG      0x15000000u
#define BASE_SNPR         0x16000000u
#define CFG_REGS_LAST_OFF 0x8u          // CFG_REGS_LAST_REG_OFFSET

// ---------------------------------------------------------------------------
// CVA6 scratch registers (accessible from cluster via AXI → Cheshire)
// ---------------------------------------------------------------------------
#define HOST_REGS_BASE       0x03000000u
#define SCRATCH_10_OFF       0x28u       // HOST_SCRATCH_10_REG_OFFSET
#define SYNC_FETCH_DONE      0xcafe00feu
#define SYNC_FETCH_DONE_RUN2 0xcafe01feu

// ---------------------------------------------------------------------------
// Snooper ring geometry
// ---------------------------------------------------------------------------
#define SNPR_RING_BYTES  16380u
#define ENTRY_SIZE       20u    // 5 × 32-bit words (PC_SRC_H/L, PC_DST_H/L, CTR_TYPE)

// ---------------------------------------------------------------------------
// L1 memory layout
//
//   l1_staging : L1_BATCH_ENTRIES snooper entries
//                = 64 × 20 B = 1280 B
//   l1_instr   : instruction bytes for one basic block at a time
//                reused each fetch; 4 KB covers all realistic block sizes
// ---------------------------------------------------------------------------
#define L1_BATCH_ENTRIES  64u
#define L1_STAGING_BYTES  (L1_BATCH_ENTRIES * ENTRY_SIZE)  // 1280 B
#define L1_INSTR_BYTES    4096u

// ---------------------------------------------------------------------------
// Helper: pointer into the shared control block
// ---------------------------------------------------------------------------
static inline volatile uint32_t *sfc(uint32_t off) {
    return (volatile uint32_t *)(SFC_CTRL_BASE + off);
}

// ---------------------------------------------------------------------------
// Helper: read a snooper config register
// ---------------------------------------------------------------------------
static inline uint32_t snpr_cfg_rd(uint32_t off) {
    return *(volatile uint32_t *)(BASE_SNPRCFG + off);
}

// ---------------------------------------------------------------------------
// Helper: read CVA6 scratch 10 (sync state from host domain)
// ---------------------------------------------------------------------------
static inline uint32_t cva6_scratch10(void) {
    return *(volatile uint32_t *)(HOST_REGS_BASE + SCRATCH_10_OFF);
}

// ---------------------------------------------------------------------------
// run1_drain — drain snooper ring with DMA + instruction fetch
//
// For each batch:
//   1. DMA batch of entries from snooper ring → L1 staging (one descriptor,
//      burst 32-bit AXI reads — this is the key advantage over Ibex).
//   2. Walk staging entries: compute basic-block instruction range, DMA
//      instruction bytes from CVA6 code memory → L1 instruction buffer.
//   3. Accumulate fetch_count (32-bit words fetched for basic blocks).
//
// Returns total 32-bit instruction words fetched.
// ---------------------------------------------------------------------------
static uint32_t run1_drain(uint32_t dummy_start, uint32_t dummy_end,
                            uint32_t *l1_staging, uint8_t *l1_instr)
{
    uint32_t drain_ptr   = 0;
    uint32_t fetch_count = 0;
    uint32_t prev_dst    = dummy_start;
    int      cva6_done   = 0;

    plp_idma_enable_clk();

    do {
        if (!cva6_done && cva6_scratch10() == SYNC_FETCH_DONE)
            cva6_done = 1;

        uint32_t cur_last = snpr_cfg_rd(CFG_REGS_LAST_OFF);
        if (drain_ptr == cur_last)
            continue;

        // Compute batch: stop at ring wrap boundary so the DMA is contiguous.
        uint32_t avail = (cur_last >= drain_ptr)
            ? (cur_last - drain_ptr)
            : (SNPR_RING_BYTES - drain_ptr);
        uint32_t batch = avail / ENTRY_SIZE;
        if (batch > L1_BATCH_ENTRIES)
            batch = L1_BATCH_ENTRIES;
        if (batch == 0)
            continue;

        // DMA batch from snooper ring → L1 staging.
        // 32-bit burst; no per-word-load overhead (advantage over Ibex).
        plp_cl_dma_wait_toL1(
            pulp_cl_idma_L2ToL1(
                BASE_SNPR + drain_ptr,
                (uint32_t)l1_staging,
                batch * ENTRY_SIZE));

        // Process each entry from L1 staging.
        // Entry word layout: [PC_SRC_L][PC_SRC_H][PC_DST_L][PC_DST_H][CTR_TYPE]
        for (uint32_t i = 0; i < batch; i++) {
            uint32_t pc_src_l = l1_staging[i * 5u + 0u];
            /* pc_src_h = l1_staging[i*5+1] — unused (physical addr, H=0) */
            uint32_t pc_dst_l = l1_staging[i * 5u + 2u];
            /* pc_dst_h, ctr_type also available at [i*5+3] and [i*5+4] */

            // Determine fetch_start: the start of this basic block.
            // If prev_dst fell outside the monitored region, fall back to pc_src.
            uint32_t fetch_start = prev_dst;
            if (fetch_start < dummy_start || fetch_start >= dummy_end)
                fetch_start = pc_src_l;

            // Guard against wrap-around on backward branches.
            uint32_t bytes = (pc_src_l >= fetch_start)
                ? (pc_src_l - fetch_start) + 4u
                : 4u;

            // Clamp to L1 instruction buffer size.
            if (bytes > L1_INSTR_BYTES)
                bytes = L1_INSTR_BYTES;

            // DMA instruction bytes from CVA6 physical code memory → L1.
            // l1_instr is reused each entry; we only accumulate the word count.
            plp_cl_dma_wait_toL1(
                pulp_cl_idma_L2ToL1(
                    fetch_start,
                    (uint32_t)l1_instr,
                    bytes));

            fetch_count += (bytes + 3u) >> 2;
            prev_dst     = pc_dst_l;
        }

        drain_ptr += batch * ENTRY_SIZE;
        if (drain_ptr >= SNPR_RING_BYTES)
            drain_ptr = 0;

    } while (!cva6_done || snpr_cfg_rd(CFG_REGS_LAST_OFF) != drain_ptr);

    plp_idma_disable_clk();
    return fetch_count;
}

// ---------------------------------------------------------------------------
// run2_drain — consume snooper ring without instruction fetch
//
// Halt is disabled during run-2 (Ibex cleared the bit and reset the ring).
// We still DMA entries out of the ring to keep it from overflowing while
// CVA6 runs the baseline measurement.  No instruction DMA is issued.
// ---------------------------------------------------------------------------
static void run2_drain(uint32_t *l1_staging)
{
    uint32_t drain_ptr  = 0;
    int      cva6_done2 = 0;

    plp_idma_enable_clk();

    do {
        if (!cva6_done2 && cva6_scratch10() == SYNC_FETCH_DONE_RUN2)
            cva6_done2 = 1;

        uint32_t cur_last = snpr_cfg_rd(CFG_REGS_LAST_OFF);
        if (drain_ptr == cur_last)
            continue;

        uint32_t avail = (cur_last >= drain_ptr)
            ? (cur_last - drain_ptr)
            : (SNPR_RING_BYTES - drain_ptr);
        uint32_t batch = avail / ENTRY_SIZE;
        if (batch > L1_BATCH_ENTRIES)
            batch = L1_BATCH_ENTRIES;
        if (batch == 0)
            continue;

        // DMA to consume entries (AXI reads advance snooper read pointer).
        plp_cl_dma_wait_toL1(
            pulp_cl_idma_L2ToL1(
                BASE_SNPR + drain_ptr,
                (uint32_t)l1_staging,
                batch * ENTRY_SIZE));

        drain_ptr += batch * ENTRY_SIZE;
        if (drain_ptr >= SNPR_RING_BYTES)
            drain_ptr = 0;

    } while (!cva6_done2 || snpr_cfg_rd(CFG_REGS_LAST_OFF) != drain_ptr);

    plp_idma_disable_clk();
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------
int main(void)
{
    uint32_t core_id = (uint32_t)rt_core_id();

    // Cores 1–7 idle at barriers matching core 0's synchronisation points.
    // Future: distribute instruction decode across cores after batch DMA.
    if (core_id != 0) {
        synch_barrier();   // sync before run-1
        synch_barrier();   // sync before run-2
        synch_barrier();   // sync before exit / doorbell
        return 0;
    }

    // -----------------------------------------------------------------------
    // Allocate L1 buffers (core 0 only)
    // -----------------------------------------------------------------------
    uint32_t *l1_staging = (uint32_t *)pi_l1_malloc(0, L1_STAGING_BYTES);
    uint8_t  *l1_instr   = (uint8_t  *)pi_l1_malloc(0, L1_INSTR_BYTES);
    if (!l1_staging || !l1_instr) {
        printf("[cl0] ERROR: L1 alloc failed\n");
        synch_barrier();
        synch_barrier();
        synch_barrier();
        hal_mailboxes_write_return_value(1);
        hal_mailboxes_ring_doorbell();
        return 1;
    }

    // -----------------------------------------------------------------------
    // Wait for Ibex to complete snooper config and signal RUN1_READY.
    // Ibex writes dummy_start/end before setting the state, so the snapshot
    // is consistent once we observe SFC_IBEX_RUN1_READY.
    // -----------------------------------------------------------------------
    while (*sfc(SFC_IBEX_STATE_OFF) != SFC_IBEX_RUN1_READY)
        ;

    uint32_t dummy_start = *sfc(SFC_DUMMY_START_OFF);
    uint32_t dummy_end   = *sfc(SFC_DUMMY_END_OFF);
    printf("[cl0] dummy region: 0x%08x - 0x%08x\n",
           (unsigned)dummy_start, (unsigned)dummy_end);

    // -----------------------------------------------------------------------
    // RUN 1: drain snooper ring via DMA + fetch instruction bytes
    // -----------------------------------------------------------------------
    synch_barrier();   // all cores ready before run-1
    uint32_t fetch_count = run1_drain(dummy_start, dummy_end, l1_staging, l1_instr);
    printf("[cl0] run-1: fetched %u 32-bit instruction words\n",
           (unsigned)fetch_count);

    // Signal Ibex: run-1 done — Ibex will now disable halt and reset ring.
    *sfc(SFC_CLUSTER_STATE_OFF) = SFC_CLUSTER_RUN1_DONE;

    // -----------------------------------------------------------------------
    // Wait for Ibex to reconfigure snooper and signal RUN2_READY
    // -----------------------------------------------------------------------
    while (*sfc(SFC_IBEX_STATE_OFF) != SFC_IBEX_RUN2_READY)
        ;

    // -----------------------------------------------------------------------
    // RUN 2: drain only (no instruction fetch; halt is disabled)
    // -----------------------------------------------------------------------
    synch_barrier();   // all cores ready before run-2
    run2_drain(l1_staging);
    printf("[cl0] run-2: drain done\n");

    // Signal Ibex: run-2 done + ring SND doorbell for Ibex to collect result.
    *sfc(SFC_CLUSTER_STATE_OFF) = SFC_CLUSTER_RUN2_DONE;
    synch_barrier();   // all cores sync before doorbell

    hal_mailboxes_write_return_value(0);
    hal_mailboxes_ring_doorbell();

    return 0;
}
