/*
 * Copyright 2026 Fondazione Chips-IT.
 * Licensed under the Apache License, Version 2.0, see LICENSE for details.
 * SPDX-License-Identifier: Apache-2.0
 *
 * cfi_preprocess_standalone/test.c — Self-contained CFI pipeline benchmark
 *
 * Overview
 * ========
 * Three hardcoded RV32/RVC instruction blocks live in L2 (static arrays).
 * Core 0 copies them into L1 via iDMA, then all 8 cluster cores run the
 * full CFI preprocessing pipeline:
 *
 *   1. [core 0] iDMA L2→L1 for each of the 3 blocks
 *   2. [core 0] Byte-walk: count instructions per block (handles RVC/RV32)
 *   3. [core 0] Build 32-instruction window (tail/head slices per block)
 *   4. [all 8]  Parallel bit-explode → 32×32 feature matrix
 *   5. [core 0] Dummy NN forward pass over the feature matrix
 *
 * No Ibex snooper, no control-register polling, no external dependencies.
 * NUM_CHAINS iterations are executed; per-stage cycle counts plus an
 * aggregate min/max/avg summary are printed at the end.
 *
 * Reference blocks (block bytes, instruction counts)
 * ---------------------------------------------------
 *   blk0: 20×RV32 + 5×RVC = 25 instructions, 90 bytes
 *   blk1: 15×RV32 + 8×RVC = 23 instructions, 76 bytes
 *   blk2: 20×RV32 + 6×RVC = 26 instructions, 92 bytes
 */

#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "pulp.h"

// ===========================================================================
// Pipeline parameters (must match cfi_preprocess reference)
// ===========================================================================
#define CFI_CHAIN_SLOTS    3u
#define WINDOW_SIZE       32u
#define BLK0_TAIL         10u
#define BLK1_HEAD          5u
#define BLK1_TAIL          5u
#define BLK2_HEAD         10u
#define NOP32             0x00000013u
#define NUM_CORES          8u
#define ROWS_PER_CORE     (WINDOW_SIZE / NUM_CORES)  // = 4
#define MAX_IDX           64u   // max instructions per slot for index table

#define NUM_CHAINS         5u    // chains to run per benchmark
#define SLOT_BYTES       256u    // L1 bytes reserved per block slot (≥ 92)
#define NN_INF_EN          0u    //activate NN_INFERENCE dummy

// ===========================================================================
// Debug verbosity — compile with -DCFI_DEBUG=1 to enable
// ===========================================================================
#ifndef CFI_DEBUG
#define CFI_DEBUG 0
#endif
#define DBG(fmt, ...) do { if (CFI_DEBUG) printf(fmt, ##__VA_ARGS__); } while (0)

// ===========================================================================
// Cycle-counter helpers (PULP cluster: custom CSRs, not standard mcycle)
// ===========================================================================
static inline void perf_init(void) {
    asm volatile ("csrw 0x79F, %0" :: "r"(0u)); // reset all counters to 0
    asm volatile ("csrw 0xCC0, %0" :: "r"(1u)); // PCER: count cycles (event 0)
    asm volatile ("csrw 0xCC1, %0" :: "r"(1u)); // PCMR: ACTIVE=1, start counting
}

static inline uint32_t rdcycle(void) {
    uint32_t c;
    asm volatile ("csrr %0, 0x780" : "=r" (c)); // counter 0 = cycles
    return c;
}

// ===========================================================================
// Per-chain statistics collected by core 0
// ===========================================================================
typedef struct {
    uint32_t cyc_copy;
    uint32_t cyc_decode;
    uint32_t cyc_window;
    uint32_t cyc_explode;   // wall-clock from core 0's perspective
    uint32_t cyc_nn;
    uint32_t n[CFI_CHAIN_SLOTS];  // instruction count per slot
    int      nn_result;
} chain_stats_t;

static chain_stats_t g_stats[NUM_CHAINS];
static int           g_alloc_err = 0;

// ===========================================================================
// L1 buffer pointers (written by core 0, broadcast via synch_barrier)
// ===========================================================================
static uint8_t  *l1_raw[CFI_CHAIN_SLOTS];
static uint32_t *l1_insn;
static uint8_t  *l1_feat;
static uint32_t  insn_count[CFI_CHAIN_SLOTS];
static uint32_t *win_idx[CFI_CHAIN_SLOTS]; // L1 byte-offset tables (allocated in startup)

// ===========================================================================
// Reference instruction blocks stored in L2 (static const)
//
// RV32 instructions: bits[1:0] == 11
// RVC  instructions: bits[1:0] != 11
//
// Little-endian byte helpers:
// ===========================================================================
#define RV32(w) \
    (uint8_t)((w)       & 0xFFu), (uint8_t)(((w) >>  8) & 0xFFu), \
    (uint8_t)(((w) >> 16) & 0xFFu), (uint8_t)(((w) >> 24) & 0xFFu)
#define RVC(h) \
    (uint8_t)((h) & 0xFFu), (uint8_t)(((h) >> 8) & 0xFFu)

// Block 0 — 20×RV32 + 5×RVC = 25 instructions, 90 bytes
static const uint8_t blk0_ref[] = {
    RV32(0x00000013u),  // addi  x0, x0, 0  (NOP)
    RV32(0x00100093u),  // addi  x1, x0, 1
    RV32(0x00200113u),  // addi  x2, x0, 2
    RV32(0x002081B3u),  // add   x3, x1, x2
    RVC (0x0001u),      // c.nop
    RV32(0x0000A203u),  // lw    x4, 0(x1)
    RV32(0x0040A023u),  // sw    x4, 0(x1)
    RVC (0x0085u),      // c.addi x1, 1
    RV32(0x402182B3u),  // sub   x5, x3, x2
    RV32(0x00109313u),  // slli  x6, x1, 1
    RV32(0x0020D393u),  // srli  x7, x1, 2
    RVC (0x0001u),      // c.nop
    RV32(0x0021F433u),  // and   x8, x3, x2
    RV32(0x0011E4B3u),  // or    x9, x3, x1
    RV32(0x00114533u),  // xor   x10, x2, x1
    RVC (0x0085u),      // c.addi x1, 1
    RV32(0x123455B7u),  // lui   x11, 0x12345
    RV32(0x00001617u),  // auipc x12, 0x1
    RV32(0x00000013u),  // NOP
    RV32(0x00000013u),  // NOP
    RVC (0x0001u),      // c.nop
    RV32(0x00000013u),  // NOP
    RV32(0x00000013u),  // NOP
    RV32(0x00000013u),  // NOP
    RV32(0x00000013u),  // NOP
};

// Block 1 — 15×RV32 + 8×RVC = 23 instructions, 76 bytes
static const uint8_t blk1_ref[] = {
    RVC (0x0001u),      // c.nop
    RVC (0x0085u),      // c.addi x1, 1
    RV32(0x00100093u),  // addi  x1, x0, 1
    RVC (0x0001u),      // c.nop
    RV32(0x00200113u),  // addi  x2, x0, 2
    RV32(0x002081B3u),  // add   x3, x1, x2
    RVC (0x0085u),      // c.addi x1, 1
    RV32(0x0000A203u),  // lw    x4, 0(x1)
    RVC (0x0001u),      // c.nop
    RV32(0x402182B3u),  // sub   x5, x3, x2
    RV32(0x00109313u),  // slli  x6, x1, 1
    RVC (0x0085u),      // c.addi x1, 1
    RV32(0x0020D393u),  // srli  x7, x1, 2
    RV32(0x0021F433u),  // and   x8, x3, x2
    RVC (0x0001u),      // c.nop
    RV32(0x0011E4B3u),  // or    x9, x3, x1
    RV32(0x00114533u),  // xor   x10, x2, x1
    RVC (0x0085u),      // c.addi x1, 1
    RV32(0x123455B7u),  // lui   x11, 0x12345
    RV32(0x00001617u),  // auipc x12, 0x1
    RV32(0x00000013u),  // NOP
    RV32(0x00000013u),  // NOP
    RV32(0x00000013u),  // NOP
};

// Block 2 — 20×RV32 + 6×RVC = 26 instructions, 92 bytes
static const uint8_t blk2_ref[] = {
    RV32(0x00000013u),  // NOP
    RVC (0x0001u),      // c.nop
    RV32(0x00100093u),  // addi  x1, x0, 1
    RV32(0x00200113u),  // addi  x2, x0, 2
    RVC (0x0085u),      // c.addi x1, 1
    RV32(0x002081B3u),  // add   x3, x1, x2
    RV32(0x0000A203u),  // lw    x4, 0(x1)
    RVC (0x0001u),      // c.nop
    RV32(0x0040A023u),  // sw    x4, 0(x1)
    RV32(0x402182B3u),  // sub   x5, x3, x2
    RVC (0x0085u),      // c.addi x1, 1
    RV32(0x00109313u),  // slli  x6, x1, 1
    RV32(0x0020D393u),  // srli  x7, x1, 2
    RV32(0x0021F433u),  // and   x8, x3, x2
    RVC (0x0001u),      // c.nop
    RV32(0x0011E4B3u),  // or    x9, x3, x1
    RV32(0x00114533u),  // xor   x10, x2, x1
    RVC (0x0085u),      // c.addi x1, 1
    RV32(0x123455B7u),  // lui   x11, 0x12345
    RV32(0x00001617u),  // auipc x12, 0x1
    RV32(0x00000013u),  // NOP
    RV32(0x00000013u),  // NOP
    RV32(0x00000013u),  // NOP
    RV32(0x00000013u),  // NOP
    RV32(0x00000013u),  // NOP
    RV32(0x00000013u),  // NOP
};

static const uint8_t  *blk_ref[CFI_CHAIN_SLOTS] = { blk0_ref, blk1_ref, blk2_ref };
static const uint32_t  blk_sz [CFI_CHAIN_SLOTS] = {
    (uint32_t)sizeof(blk0_ref),
    (uint32_t)sizeof(blk1_ref),
    (uint32_t)sizeof(blk2_ref),
};


static uint32_t decode_and_index(const uint8_t *buf, uint32_t bytes, uint32_t *idx)
{
    uint32_t n = 0, pos = 0;
    while (pos + 1u <= bytes && n < MAX_IDX) {
        idx[n] = pos;
        pos += ((buf[pos] & 0x3u) != 0x3u) ? 2u : 4u;
        n++;
    }
    return n;
}

static inline uint32_t read_insn(const uint8_t *buf, uint32_t off)
{
    if ((buf[off] & 0x3u) != 0x3u)
        return (uint32_t)buf[off] | ((uint32_t)buf[off + 1u] << 8u);
    return (uint32_t)buf[off]               |
           ((uint32_t)buf[off + 1u] <<  8u) |
           ((uint32_t)buf[off + 2u] << 16u) |
           ((uint32_t)buf[off + 3u] << 24u);
}

static inline void explode_insn(uint32_t insn, uint8_t *out)
{
    for (uint32_t b = 0; b < 32u; b++)
        out[b] = (uint8_t)((insn >> b) & 0x1u);
}

static int dummy_nn_inference(const uint8_t *feat, uint32_t rows, uint32_t cols)
{
    volatile uint32_t acc = 0;
    for (uint32_t r = 0; r < rows; r++)
        for (uint32_t c = 0; c < cols; c++)
            acc += (uint32_t)feat[r * cols + c] * (r * cols + c + 1u);
    return (acc != 0u) ? 1 : 0;
}

// ===========================================================================
// main
// ===========================================================================
int main(void)
{
    uint32_t core_id = (uint32_t)rt_core_id();

    // -----------------------------------------------------------------------
    // Startup: core 0 allocates L1 buffers; all cores wait
    // -----------------------------------------------------------------------
    if (core_id == 0) {
        perf_init();
        printf("[standalone] CFI pipeline benchmark — %u chains, %u cores\r\n",
               (unsigned)NUM_CHAINS, (unsigned)NUM_CORES);

        g_alloc_err = 0;
        for (uint32_t k = 0; k < CFI_CHAIN_SLOTS; k++) {
            l1_raw[k] = (uint8_t *)pi_l1_malloc(0, SLOT_BYTES);
            if (!l1_raw[k]) {
                printf("[cl0] ERROR: L1 alloc failed for slot %u\r\n", k);
                g_alloc_err = 1;
            }
        }
        l1_insn = (uint32_t *)pi_l1_malloc(0, WINDOW_SIZE * sizeof(uint32_t));
        l1_feat = (uint8_t  *)pi_l1_malloc(0, WINDOW_SIZE * 32u);
        if (!l1_insn || !l1_feat) {
            printf("[cl0] ERROR: L1 alloc for insn/feat buffers failed\r\n");
            g_alloc_err = 1;
        }
        for (uint32_t k = 0; k < CFI_CHAIN_SLOTS; k++) {
            win_idx[k] = (uint32_t *)pi_l1_malloc(0, MAX_IDX * sizeof(uint32_t));
            if (!win_idx[k]) {
                printf("[cl0] ERROR: L1 alloc for win_idx[%u] failed\r\n", k);
                g_alloc_err = 1;
            }
        }
    }

    synch_barrier();

    if (g_alloc_err) {
        if (core_id == 0) {
            hal_mailboxes_write_return_value(1);
            hal_mailboxes_ring_doorbell();
        }
        return 1;
    }

    // -----------------------------------------------------------------------
    // Chain loop
    // -----------------------------------------------------------------------
    for (uint32_t chain = 0; chain < NUM_CHAINS; chain++) {

        // -------------------------------------------------------------------
        // Stage 1: COPY — iDMA L2 → L1 (core 0)
        // -------------------------------------------------------------------
        if (core_id == 0) {
            DBG("[dbg:S1] chain=%u DMA start\r\n", (unsigned)chain);
            uint32_t t0 = rdcycle();
            plp_idma_enable_clk();
            for (uint32_t k = 0; k < CFI_CHAIN_SLOTS; k++) {
                uint32_t src = (uint32_t)blk_ref[k];
                uint32_t dst = (uint32_t)l1_raw[k];
                DBG("  [dbg:S1] slot %u: L2=0x%08x -> L1=0x%08x sz=%u\r\n",
                    (unsigned)k, (unsigned)src, (unsigned)dst, (unsigned)blk_sz[k]);
                plp_cl_dma_wait_toL1(
                    pulp_cl_idma_L2ToL1(src, dst, blk_sz[k]));
            }
            plp_idma_disable_clk();
            g_stats[chain].cyc_copy = rdcycle() - t0;
            DBG("[dbg:S1] chain=%u COPY done cyc=%u\r\n",
                (unsigned)chain, (unsigned)g_stats[chain].cyc_copy);
        }

        synch_barrier();

        // -------------------------------------------------------------------
        // Stage 2: DECODE — count instructions per slot (parallel: 1 core/slot)
        //   Core 0 → slot 0, Core 1 → slot 1, Core 2 → slot 2; cores 3-7 idle.
        // -------------------------------------------------------------------
        uint32_t t_dec0 = 0;
        if (core_id == 0) {
            t_dec0 = rdcycle();
            DBG("[dbg:S2] chain=%u DECODE start (parallel)\r\n", (unsigned)chain);
        }

        if (core_id < CFI_CHAIN_SLOTS) {
            insn_count[core_id]       = decode_and_index(l1_raw[core_id], blk_sz[core_id], win_idx[core_id]);
            g_stats[chain].n[core_id] = insn_count[core_id];
        }

        synch_barrier();  // insn_count[] visible to all cores

        if (core_id == 0) {
            g_stats[chain].cyc_decode = rdcycle() - t_dec0;
            if (CFI_DEBUG) {
                for (uint32_t k = 0; k < CFI_CHAIN_SLOTS; k++) {
                    printf("  [dbg:S2] slot %u: sz=%u insns=%u\r\n",
                           (unsigned)k, (unsigned)blk_sz[k], (unsigned)insn_count[k]);
                    uint32_t p = 0;
                    for (uint32_t i = 0; i < insn_count[k]; i++) {
                        int rvc = ((l1_raw[k][p] & 0x3u) != 0x3u);
                        uint32_t insn = read_insn(l1_raw[k], p);
                        printf("    [%2u] %s 0x%0*x\r\n",
                               (unsigned)i, rvc ? "RVC " : "RV32",
                               rvc ? 4 : 8, (unsigned)insn);
                        p += rvc ? 2u : 4u;
                    }
                }
                printf("[dbg:S2] chain=%u DECODE done cyc=%u\r\n",
                       (unsigned)chain, (unsigned)g_stats[chain].cyc_decode);
            }
        }

        // -------------------------------------------------------------------
        // Stage 3: WINDOW BUILD — extract 32-instruction window
        //
        // Phase A [core 0]: pre-fill NOP32; build per-slot byte-offset tables
        // Phase B [all 8]:  parallel fill — each core writes its 4 slots
        //
        // Fixed window layout:
        //   win[ 0.. 9]  blk0 tail  (last  min(n0,10) insns of blk0)
        //   win[10..14]  blk1 head  (first min(n1, 5) insns of blk1)
        //   win[15..19]  blk1 tail  (last  min(n1, 5) insns of blk1)
        //   win[20..29]  blk2 head  (first min(n2,10) insns of blk2)
        //   win[30..31]  NOP32 padding
        // -------------------------------------------------------------------
        uint32_t t_win0 = 0;

        // Phase A — core 0: NOP32 pre-fill + build index tables
        if (core_id == 0) {
            t_win0 = rdcycle();

            for (uint32_t j = 0; j < WINDOW_SIZE; j++) l1_insn[j] = NOP32;

            DBG("[dbg:S3] chain=%u n0=%u n1=%u n2=%u — parallel fill\r\n",
                (unsigned)chain, (unsigned)insn_count[0],
                (unsigned)insn_count[1], (unsigned)insn_count[2]);
            DBG("  layout: blk0_tail[0..10) blk1_head[10..15) blk1_tail[15..20) blk2_head[20..30) pad[30..32)\r\n");
        }

        synch_barrier();  // idx tables + NOP32 pre-fill visible to all cores

        // Phase B — all 8 cores: each fills its ROWS_PER_CORE (4) window slots
        {
            // Compile-time section boundaries
            const uint32_t S1 = BLK0_TAIL;               //  0..10 → blk0 tail
            const uint32_t S2 = BLK0_TAIL + BLK1_HEAD;   // 10..15 → blk1 head
            const uint32_t S3 = S2 + BLK1_TAIL;          // 15..20 → blk1 tail
            const uint32_t S4 = S3 + BLK2_HEAD;          // 20..30 → blk2 head

            uint32_t n0 = insn_count[0];
            uint32_t n1 = insn_count[1];
            uint32_t n2 = insn_count[2];

            uint32_t row_start = core_id * ROWS_PER_CORE;
            uint32_t row_end   = row_start + ROWS_PER_CORE;

            for (uint32_t wi = row_start; wi < row_end; wi++) {
                uint32_t insn = NOP32;
                if (wi < S1) {
                    // blk0 tail: skip first (n0 - BLK0_TAIL) insns
                    uint32_t skip = (n0 > BLK0_TAIL) ? (n0 - BLK0_TAIL) : 0u;
                    uint32_t i = skip + wi;
                    if (i < n0) insn = read_insn(l1_raw[0], win_idx[0][i]);
                } else if (wi < S2) {
                    // blk1 head
                    uint32_t i = wi - S1;
                    if (i < n1) insn = read_insn(l1_raw[1], win_idx[1][i]);
                } else if (wi < S3) {
                    // blk1 tail: skip first (n1 - BLK1_TAIL) insns
                    uint32_t skip = (n1 > BLK1_TAIL) ? (n1 - BLK1_TAIL) : 0u;
                    uint32_t i = skip + (wi - S2);
                    if (i < n1) insn = read_insn(l1_raw[1], win_idx[1][i]);
                } else if (wi < S4) {
                    // blk2 head
                    uint32_t i = wi - S3;
                    if (i < n2) insn = read_insn(l1_raw[2], win_idx[2][i]);
                }
                l1_insn[wi] = insn;
            }
        }

        synch_barrier();  // l1_insn[] complete — visible to all cores

        if (core_id == 0) {
            g_stats[chain].cyc_window = rdcycle() - t_win0;
            if (CFI_DEBUG) {
                printf("  window[0..31] (after parallel fill):\r\n");
                for (uint32_t j = 0; j < WINDOW_SIZE; j++)
                    printf("    [%2u] 0x%08x%s\r\n",
                           (unsigned)j, (unsigned)l1_insn[j],
                           (l1_insn[j] == NOP32) ? " (NOP/pad)" : "");
                printf("[dbg:S3] chain=%u WINDOW done cyc=%u\r\n",
                       (unsigned)chain, (unsigned)g_stats[chain].cyc_window);
            }
        }

        // -------------------------------------------------------------------
        // Stage 4: BIT-EXPLODE — parallel across 8 cores
        //   Each core fills ROWS_PER_CORE (=4) rows of l1_feat.
        //   Core 0 records wall-clock time spanning the full parallel work.
        // -------------------------------------------------------------------
        uint32_t t_exp0 = 0;
        if (core_id == 0) t_exp0 = rdcycle();

        {
            uint32_t row_start = core_id * ROWS_PER_CORE;
            uint32_t row_end   = row_start + ROWS_PER_CORE;
            for (uint32_t r = row_start; r < row_end; r++)
                explode_insn(l1_insn[r], &l1_feat[r * 32u]);
        }

        synch_barrier();  // l1_feat[] complete

        if (core_id == 0) {
            g_stats[chain].cyc_explode = rdcycle() - t_exp0;
            if (CFI_DEBUG) {
                printf("[dbg:S4] chain=%u EXPLODE done cyc=%u — feat[0..3]:\r\n",
                       (unsigned)chain, (unsigned)g_stats[chain].cyc_explode);
                for (uint32_t r = 0; r < WINDOW_SIZE; r++) {
                    printf("  row[%u] 0x%08x:", (unsigned)r, (unsigned)l1_insn[r]);
                    for (uint32_t b = 0; b < 32u; b++)
                        printf(" %u", (unsigned)l1_feat[r * 32u + b]);
                    printf("\r\n");
                }
            }
        }

        // -------------------------------------------------------------------
        // Stage 5: NN INFERENCE — dummy forward pass (core 0)
        // -------------------------------------------------------------------
        if(NN_INF_EN){
            if (core_id == 0) {
                uint32_t t0 = rdcycle();
                int nn = dummy_nn_inference(l1_feat, WINDOW_SIZE, 32u);
                g_stats[chain].cyc_nn    = rdcycle() - t0;
                g_stats[chain].nn_result = nn;
                DBG("[dbg:S5] chain=%u NN result=%d cyc=%u\r\n",
                    (unsigned)chain, nn, (unsigned)g_stats[chain].cyc_nn);

                uint32_t total = g_stats[chain].cyc_copy   +
                                g_stats[chain].cyc_decode  +
                                g_stats[chain].cyc_window  +
                                g_stats[chain].cyc_explode +
                                g_stats[chain].cyc_nn;

                printf("[chain %u] n=%u/%u/%u  "
                    "copy=%u dec=%u win=%u exp=%u nn=%u tot=%u  nn=%d\r\n",
                    (unsigned)chain,
                    (unsigned)g_stats[chain].n[0],
                    (unsigned)g_stats[chain].n[1],
                    (unsigned)g_stats[chain].n[2],
                    (unsigned)g_stats[chain].cyc_copy,
                    (unsigned)g_stats[chain].cyc_decode,
                    (unsigned)g_stats[chain].cyc_window,
                    (unsigned)g_stats[chain].cyc_explode,
                    (unsigned)g_stats[chain].cyc_nn,
                    (unsigned)total,
                    nn);
            }

            synch_barrier();
        }
    }
        

    // -----------------------------------------------------------------------
    // Aggregate statistics (core 0)
    // -----------------------------------------------------------------------
    if (core_id == 0) {
        uint32_t mn[6], mx[6], sm[6];
        for (uint32_t s = 0; s < 6; s++) { mn[s] = ~0u; mx[s] = 0u; sm[s] = 0u; }

        for (uint32_t i = 1; i < NUM_CHAINS; i++) {
            uint32_t v[6] = {
                g_stats[i].cyc_copy,
                g_stats[i].cyc_decode,
                g_stats[i].cyc_window,
                g_stats[i].cyc_explode,
                g_stats[i].cyc_nn,
                g_stats[i].cyc_copy  + g_stats[i].cyc_decode +
                g_stats[i].cyc_window + g_stats[i].cyc_explode +
                g_stats[i].cyc_nn
            };
            for (uint32_t s = 0; s < 6; s++) {
                if (v[s] < mn[s]) mn[s] = v[s];
                if (v[s] > mx[s]) mx[s] = v[s];
                sm[s] += v[s];
            }
        }
        for (uint32_t i = 0; i < NUM_CHAINS; i++) {
            uint32_t v[6] = {
                g_stats[i].cyc_copy,
                g_stats[i].cyc_decode,
                g_stats[i].cyc_window,
                g_stats[i].cyc_explode,
                g_stats[i].cyc_nn,
                g_stats[i].cyc_copy  + g_stats[i].cyc_decode +
                g_stats[i].cyc_window + g_stats[i].cyc_explode +
                g_stats[i].cyc_nn
            };
            printf("\r\n=== Non Aggregate Stats (%u chains) ===\r\n", (unsigned)NUM_CHAINS);
            printf("%-8s  %8s\r\n", "stage", "cycles");

            const char *labels[6] = { "copy", "decode", "window", "explode", "nn", "TOTAL" };
            for (uint32_t s = 0; s < 6; s++) {
                printf("%-8s  %8u\r\n",
                    labels[s],
                    (unsigned)v[s]);
            }
        }

        printf("\r\n=== Aggregate Stats (%u chains) ===\r\n", (unsigned)NUM_CHAINS);
        printf("%-8s  %8s  %8s  %8s\r\n", "stage", "min", "max", "avg");

        const char *labels[6] = { "copy", "decode", "window", "explode", "nn", "TOTAL" };
        for (uint32_t s = 0; s < 6; s++) {
            printf("%-8s  %8u  %8u  %8u\r\n",
                   labels[s],
                   (unsigned)mn[s],
                   (unsigned)mx[s],
                   (unsigned)(sm[s] / NUM_CHAINS));
        }

        // hal_mailboxes_write_return_value(0);
        // hal_mailboxes_ring_doorbell();
    }

    synch_barrier();
    return 0;
}
