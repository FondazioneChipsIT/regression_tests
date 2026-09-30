/*
 * Copyright 2026 Fondazione Chips-IT.
 * Licensed under the Apache License, Version 2.0, see LICENSE for details.
 * SPDX-License-Identifier: Apache-2.0
 *
 * cfi_preprocess/test.c — PULP cluster side of the CFI pipeline
 *
 * Overview
 * ========
 * Ibex (snooper) fills L2 shared memory with raw instruction bytes fetched
 * from CVA6's pipeline, three basic blocks at a time ("a chain").  It then
 * writes a control block at CFI_CTRL_BASE that describes the chain layout
 * and signals CTRL_IBEX_CHAIN_READY.
 *
 * This cluster program runs an infinite service loop:
 *
 *   1. [core 0] Signal Ibex that cluster is alive  (init handshake).
 *   2. [core 0] Poll CFI_CTRL_IBEX_READY_OFF for CTRL_IBEX_CHAIN_READY.
 *              On CTRL_CLUSTER_EXIT, ring the SND doorbell and stop.
 *   3. [core 0] iDMA: copy the three basic-block slots from L2 → L1.
 *   4. [all 8]  Parallel instruction decode: for each block, scan bytes
 *              and count instructions (RVC = 2-byte, RV32 = 4-byte).
 *   5. [all 8]  Parallel window extraction into a 32-instruction array:
 *                - block 0 : last  min(n0, 10) instructions
 *                - block 1 : first 5  + last 5 instructions
 *                - block 2 : first min(n2, 10) instructions
 *              Total: up to 32 instructions (padded with NOP if fewer).
 *   6. [all 8]  Bit-explode: each 32-bit instruction → 32 bytes (one bit
 *              per byte, LSB first).  Result: 32 × 32 = 1024-byte array.
 *   7. [core 0] Dummy NN loop (simulates inference over the 32×32 input).
 *   8. [core 0] Write CTRL_CLUSTER_DONE, go back to step 2.
 *
 * Memory layout (L1, allocated once at startup)
 * ----------------------------------------------
 *   l1_raw[k]    : raw instruction bytes for slot k (CFI_INSTR_SLOT_BYTES)
 *   insn[32]     : extracted 32-bit instruction window
 *   feat[32][32] : bit-exploded feature matrix (1024 bytes)
 *
 * Parallelism
 * -----------
 *   Steps 4-6 divide the 32 output rows (instructions) across 8 cores:
 *   each core handles 4 rows (ROWS_PER_CORE = 32 / NUM_CORES = 4).
 *   Step 5 extraction: core 0 pre-builds the index table (trivial),
 *   all cores then fill their 4 rows of feat[] in parallel.
 *
 * Sync between Ibex and cluster
 * ------------------------------
 *   Ibex → cluster : CTRL_IBEX_CHAIN_READY  at CFI_CTRL_IBEX_READY_OFF
 *   Cluster → Ibex : CTRL_CLUSTER_DONE      at CFI_CTRL_CLUSTER_DONE_OFF
 *   The cluster clears ibex_ready after reading it and writes cluster_done
 *   after completing the chain.  No hardware interrupts needed on either
 *   side — pure polling keeps latency minimal.
 */

#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "pulp.h"

// ---------------------------------------------------------------------------
// Shared control-block definitions (must match cfi_fetch_cluster.h)
// ---------------------------------------------------------------------------
#define CFI_CTRL_BASE              0xA0010000u
#define CFI_INSTR_BASE             0xA0010040u

#define CFI_CTRL_IBEX_READY_OFF    0x00u
#define CFI_CTRL_CHAIN_ID_OFF      0x04u
#define CFI_CTRL_CLUSTER_DONE_OFF  0x08u
#define CFI_CTRL_BLK0_OFF_OFF      0x0Cu
#define CFI_CTRL_BLK1_OFF_OFF      0x10u
#define CFI_CTRL_BLK2_OFF_OFF      0x14u
#define CFI_CTRL_BLK0_SZ_OFF       0x18u
#define CFI_CTRL_BLK1_SZ_OFF       0x1Cu
#define CFI_CTRL_BLK2_SZ_OFF       0x20u

#define CTRL_IDLE                  0x00000000u
#define CTRL_IBEX_CHAIN_READY      0xCAFE0001u
#define CTRL_CLUSTER_DONE          0xDEAD0002u
#define CTRL_CLUSTER_EXIT          0xDEAD0003u

#define CFI_CHAIN_SLOTS            3u
#define CFI_INSTR_SLOT_BYTES       4096u

// ---------------------------------------------------------------------------
// Window extraction parameters
// ---------------------------------------------------------------------------
#define WINDOW_SIZE        32u   // total instructions in the window
#define BLK0_TAIL          10u   // last N insns of block 0
#define BLK1_HEAD           5u   // first M insns of block 1
#define BLK1_TAIL           5u   // last  M insns of block 1
#define BLK2_HEAD          10u   // first N insns of block 2
// BLK0_TAIL + BLK1_HEAD + BLK1_TAIL + BLK2_HEAD = 30, padded to 32 with NOP

#define NOP32              0x00000013u  // RISC-V NOP (addi x0,x0,0)

// ---------------------------------------------------------------------------
// Parallelism
// ---------------------------------------------------------------------------
#define NUM_CORES           8u
#define ROWS_PER_CORE      (WINDOW_SIZE / NUM_CORES)   // = 4

// ---------------------------------------------------------------------------
// L1 buffers (allocated once; reused across chains)
// ---------------------------------------------------------------------------
// Declared at file scope so pi_l1_malloc is called once from core 0 and
// the pointers are broadcast via synch_barrier + shared L1 globals.
static uint8_t  *l1_raw[CFI_CHAIN_SLOTS];   // raw bytes per slot
static uint32_t *l1_insn;                    // 32-entry instruction window
static uint8_t  *l1_feat;                    // 32×32 bit-exploded matrix
// scratch counters: number of instructions decoded in each slot
static uint32_t  insn_count[CFI_CHAIN_SLOTS];

// ---------------------------------------------------------------------------
// ctrl helpers
// ---------------------------------------------------------------------------
static inline volatile uint32_t *ctrl(uint32_t off) {
    return (volatile uint32_t *)(CFI_CTRL_BASE + off);
}

// ---------------------------------------------------------------------------
// Instruction counting: walk bytes, detect C-extension (16-bit) vs full (32-bit)
// Returns number of instructions in 'bytes' bytes of raw data.
// ---------------------------------------------------------------------------
static uint32_t count_insns(const uint8_t *buf, uint32_t bytes) {
    uint32_t n   = 0;
    uint32_t pos = 0;
    while (pos + 1u <= bytes) {
        uint8_t low2 = buf[pos] & 0x3u;
        if (low2 != 0x3u) {
            // RVC (16-bit): op[1:0] != 11
            pos += 2u;
        } else {
            // RV32 (32-bit): op[1:0] == 11
            pos += 4u;
        }
        n++;
    }
    return n;
}

// ---------------------------------------------------------------------------
// Read a 32-bit instruction at byte offset (handles C-extension padding)
// ---------------------------------------------------------------------------
static inline uint32_t read_insn(const uint8_t *buf, uint32_t off) {
    uint8_t low2 = buf[off] & 0x3u;
    if (low2 != 0x3u) {
        // 16-bit compressed: zero-extend
        return (uint32_t)buf[off] | ((uint32_t)buf[off + 1u] << 8u);
    }
    return (uint32_t)buf[off]         |
           ((uint32_t)buf[off + 1u] << 8u)  |
           ((uint32_t)buf[off + 2u] << 16u) |
           ((uint32_t)buf[off + 3u] << 24u);
}

// ---------------------------------------------------------------------------
// Bit-explode one 32-bit instruction into 32 bytes (bit 0 in byte 0)
// Each byte is 0x00 or 0x01.
// ---------------------------------------------------------------------------
static inline void explode_insn(uint32_t insn, uint8_t *out) {
    for (uint32_t b = 0; b < 32u; b++)
        out[b] = (uint8_t)((insn >> b) & 0x1u);
}

// ---------------------------------------------------------------------------
// Dummy NN forward pass (simulates inference over the 32×32 feature map)
// Uses a simple accumulate-and-threshold loop so the compiler cannot elide it.
// ---------------------------------------------------------------------------
static int dummy_nn_inference(const uint8_t *feat, uint32_t rows, uint32_t cols) {
    volatile uint32_t acc = 0;
    for (uint32_t r = 0; r < rows; r++) {
        for (uint32_t c = 0; c < cols; c++) {
            // Fake weight = (r*cols + c + 1); bias = 0
            acc += (uint32_t)feat[r * cols + c] * ((r * cols + c) + 1u);
        }
    }
    // Threshold: non-zero → "anomaly detected", 0 → "clean"
    return (acc != 0u) ? 1 : 0;
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------
int main(void)
{
    uint32_t core_id = (uint32_t)rt_core_id();
    int total_errors = 0;

    // -----------------------------------------------------------------------
    // Startup: allocate L1 buffers (core 0 only)
    // -----------------------------------------------------------------------
    if (core_id == 0) {
        for (uint32_t k = 0; k < CFI_CHAIN_SLOTS; k++) {
            l1_raw[k] = (uint8_t *)pi_l1_malloc(0, CFI_INSTR_SLOT_BYTES);
            if (!l1_raw[k]) {
                printf("[cl0] ERROR: L1 alloc failed for slot %u\n", k);
                total_errors++;
            }
        }
        l1_insn = (uint32_t *)pi_l1_malloc(0, WINDOW_SIZE * sizeof(uint32_t));
        l1_feat = (uint8_t  *)pi_l1_malloc(0, WINDOW_SIZE * 32u);
        if (!l1_insn || !l1_feat) {
            printf("[cl0] ERROR: L1 alloc for insn/feat buffers failed\n");
            total_errors++;
        }
        printf("[cl0] cfi_preprocess: buffers allocated, sending ready ping\n");

        // --- Init handshake: tell Ibex we're alive ---
        // Write CTRL_CLUSTER_DONE so Ibex's wait_cluster_done() unblocks.
        *ctrl(CFI_CTRL_CLUSTER_DONE_OFF) = CTRL_CLUSTER_DONE;
    }

    synch_barrier();

    if (total_errors) {
        // Can't proceed without buffers; ring doorbell with error code
        if (core_id == 0) {
            hal_mailboxes_write_return_value(total_errors);
            hal_mailboxes_ring_doorbell();
        }
        return total_errors;
    }

    // -----------------------------------------------------------------------
    // Service loop
    // -----------------------------------------------------------------------
    uint32_t chains_processed = 0;

    while (1) {
        // -------------------------------------------------------------------
        // [core 0] Poll for next chain or exit signal
        // -------------------------------------------------------------------
        if (core_id == 0) {
            uint32_t sig;
            do {
                sig = *ctrl(CFI_CTRL_IBEX_READY_OFF);
            } while (sig == CTRL_IDLE);

            if (sig == CTRL_CLUSTER_EXIT) {
                // Ibex is done — ring the SND doorbell and stop
                printf("[cl0] received EXIT after %u chains\n",
                       (unsigned)chains_processed);
                hal_mailboxes_write_return_value(0);
                hal_mailboxes_ring_doorbell();
                // Clear ready flag before leaving
                *ctrl(CFI_CTRL_IBEX_READY_OFF) = CTRL_IDLE;
                synch_barrier();
                return 0;
            }

            // Snapshot the full chain descriptor before clearing the ready flag.
            // blk_off[k]: byte offset from CFI_INSTR_BASE where block k starts.
            // blk_sz[k]:  byte length of block k (0 = unused slot in tail chain).
            // With rotating slots the offsets are NOT always k*SLOT_BYTES, so we
            // must read them from the control block written by Ibex.
            uint32_t blk_off_snap[CFI_CHAIN_SLOTS];
            uint32_t byte_sz[CFI_CHAIN_SLOTS];
            blk_off_snap[0] = *ctrl(CFI_CTRL_BLK0_OFF_OFF);
            blk_off_snap[1] = *ctrl(CFI_CTRL_BLK1_OFF_OFF);
            blk_off_snap[2] = *ctrl(CFI_CTRL_BLK2_OFF_OFF);
            byte_sz[0]      = *ctrl(CFI_CTRL_BLK0_SZ_OFF);
            byte_sz[1]      = *ctrl(CFI_CTRL_BLK1_SZ_OFF);
            byte_sz[2]      = *ctrl(CFI_CTRL_BLK2_SZ_OFF);
            // Clear ready so Ibex can proceed to the next entry immediately
            *ctrl(CFI_CTRL_IBEX_READY_OFF) = CTRL_IDLE;

            // Copy byte sizes into insn_count[]; will be converted to counts below.
            insn_count[0] = byte_sz[0];
            insn_count[1] = byte_sz[1];
            insn_count[2] = byte_sz[2];

            // ---------------------------------------------------------------
            // [core 0] iDMA L2 → L1: copy each block using its actual L2
            //   offset (blk_off_snap[k]) rather than a fixed stride so that
            //   rotating slots are handled correctly.
            // ---------------------------------------------------------------
            plp_idma_enable_clk();
            for (uint32_t k = 0; k < CFI_CHAIN_SLOTS; k++) {
                uint32_t sz = byte_sz[k];
                if (sz == 0u) continue;
                uint32_t src = CFI_INSTR_BASE + blk_off_snap[k];
                uint32_t dst = (uint32_t)l1_raw[k];
                plp_cl_dma_wait_toL1(
                    pulp_cl_idma_L2ToL1(src, dst, sz));
            }
            plp_idma_disable_clk();

            // Convert byte sizes to instruction counts
            for (uint32_t k = 0; k < CFI_CHAIN_SLOTS; k++)
                insn_count[k] = count_insns(l1_raw[k], byte_sz[k]);
        }

        synch_barrier();  // wait for DMA + count to be visible to all cores

        // -------------------------------------------------------------------
        // [core 0] Build the 32-instruction window
        //
        //   Window layout:
        //     [0  .. BLK0_TAIL-1]      last min(n0, BLK0_TAIL) insns of blk0
        //     [BLK0_TAIL .. BLK0_TAIL+BLK1_HEAD-1]   first BLK1_HEAD of blk1
        //     [BLK0_TAIL+BLK1_HEAD .. BLK0_TAIL+BLK1_HEAD+BLK1_TAIL-1]
        //                              last BLK1_TAIL of blk1
        //     [30 .. 31]               first min(n2, BLK2_HEAD) insns of blk2
        //     remaining slots          padded with NOP32
        //
        //   Index tables are local (stack); sizes are small (≤ max insn count
        //   per slot ≤ CFI_INSTR_SLOT_BYTES / 2 = 2048, but we only need ≤30).
        // -------------------------------------------------------------------
        if (core_id == 0) {
            // Initialise window to NOP
            for (uint32_t j = 0; j < WINDOW_SIZE; j++) l1_insn[j] = NOP32;

            // Build per-slot byte-offset index tables from L1 raw bytes.
            // insn_count[k] holds the number of instructions in slot k.
            // We need at most BLK0_TAIL+BLK1_HEAD+BLK1_TAIL+BLK2_HEAD=30 per slot.
            #define MAX_IDX 2048u
            static uint32_t idx0[MAX_IDX], idx1[MAX_IDX], idx2[MAX_IDX];

            uint32_t n0 = insn_count[0];
            uint32_t n1 = insn_count[1];
            uint32_t n2 = insn_count[2];

            // Rebuild byte-offset index arrays from L1 raw data
            {
                uint32_t p = 0;
                for (uint32_t c = 0; c < n0 && c < MAX_IDX; c++) {
                    idx0[c] = p;
                    p += ((l1_raw[0][p] & 0x3u) != 0x3u) ? 2u : 4u;
                }
            }
            {
                uint32_t p = 0;
                for (uint32_t c = 0; c < n1 && c < MAX_IDX; c++) {
                    idx1[c] = p;
                    p += ((l1_raw[1][p] & 0x3u) != 0x3u) ? 2u : 4u;
                }
            }
            {
                uint32_t p = 0;
                for (uint32_t c = 0; c < n2 && c < MAX_IDX; c++) {
                    idx2[c] = p;
                    p += ((l1_raw[2][p] & 0x3u) != 0x3u) ? 2u : 4u;
                }
            }

            uint32_t wi = 0;

            // Block 0: last min(n0, BLK0_TAIL) instructions
            {
                uint32_t take = (n0 < BLK0_TAIL) ? n0 : BLK0_TAIL;
                uint32_t start = n0 - take;
                for (uint32_t j = start; j < n0 && wi < WINDOW_SIZE; j++, wi++)
                    l1_insn[wi] = read_insn(l1_raw[0], idx0[j]);
            }

            // Block 1: first BLK1_HEAD instructions
            {
                uint32_t take = (n1 < BLK1_HEAD) ? n1 : BLK1_HEAD;
                for (uint32_t j = 0; j < take && wi < WINDOW_SIZE; j++, wi++)
                    l1_insn[wi] = read_insn(l1_raw[1], idx1[j]);
            }

            // Block 1: last BLK1_TAIL instructions
            {
                uint32_t take = (n1 < BLK1_TAIL) ? n1 : BLK1_TAIL;
                uint32_t start = n1 - take;
                for (uint32_t j = start; j < n1 && wi < WINDOW_SIZE; j++, wi++)
                    l1_insn[wi] = read_insn(l1_raw[1], idx1[j]);
            }

            // Block 2: first BLK2_HEAD instructions
            {
                uint32_t take = (n2 < BLK2_HEAD) ? n2 : BLK2_HEAD;
                for (uint32_t j = 0; j < take && wi < WINDOW_SIZE; j++, wi++)
                    l1_insn[wi] = read_insn(l1_raw[2], idx2[j]);
            }
            // Remaining slots already NOP32 from init above.
            (void)wi;
        }

        synch_barrier();  // l1_insn[] visible to all cores

        // -------------------------------------------------------------------
        // [all 8 cores] Parallel bit-explode: each core fills ROWS_PER_CORE
        // rows of l1_feat[row][bit]  (row-major, 32 bytes per row).
        // -------------------------------------------------------------------
        {
            uint32_t row_start = core_id * ROWS_PER_CORE;
            uint32_t row_end   = row_start + ROWS_PER_CORE;
            for (uint32_t r = row_start; r < row_end; r++) {
                explode_insn(l1_insn[r], &l1_feat[r * 32u]);
            }
        }

        synch_barrier();  // l1_feat[] complete

        // -------------------------------------------------------------------
        // [core 0] Dummy NN inference over the 32×32 feature matrix
        // -------------------------------------------------------------------
        if (core_id == 0) {
            //int nn_result = dummy_nn_inference(l1_feat, WINDOW_SIZE, 32u);
            chains_processed++;
            // printf("[cl0] chain %u  nn=%d\r\n", (unsigned)chains_processed, nn_result);

            // Signal Ibex: chain preprocessing complete
            *ctrl(CFI_CTRL_CLUSTER_DONE_OFF) = CTRL_CLUSTER_DONE;
        }

        synch_barrier();  // all cores wait before polling the next chain
    }

    // Unreachable
    return 0;
}
