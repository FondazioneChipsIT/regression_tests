/*
 * Copyright 2026 Fondazione Chips-IT.
 * Licensed under the Apache License, Version 2.0, see LICENSE for details.
 * SPDX-License-Identifier: Apache-2.0
 *
 * cfi_syscall_cluster_preprocess/test.c
 *
 * Cluster CFI inspector (cfi_syscall_cluster) + preprocessing pipeline
 * (cfi_preprocess_standalone), merged: the pipeline runs on the basic blocks
 * the inspector fetches from the snooper, not hardcoded L2 blocks. The two fit
 * directly — CFI_CHAIN_SLOTS == CFI_WINDOW_SIZE == 3, and l1_instr[k]/
 * fetch_bytes[k] are the pipeline's per-slot inputs.
 *
 * Per syscall (core 0 drives, all 8 cores compute):
 *   [c0]  doorbell -> inspect_window(): last 3 ring entries + DMA their BB bytes
 *   -- barrier --
 *   [all] preprocess_pipeline(): DECODE -> WINDOW (32 insn) -> EXPLODE (32x32
 *         feat) -> NN
 *   [c0]  scratch14 <- PASS / VIOLATION
 *
 * main() is a controller/worker barrier loop: all cores run the same barrier
 * sequence each round (branch on shared g_msg_type), so the barrier never
 * desyncs. The CVA6 monitor SIGKILLs the child on VIOLATION, so the dummy NN
 * always reports PASS; -DNN_FLAG_EVERY=N forces periodic violations.
 */

#include <stdio.h>
#include <stdint.h>
#include "pulp.h"

#include "cfi_va_pa_table.h"    // VA->PA table + scratch 12/13 protocol
#include "cfi_syscall_proto.h"  // CFI_MSG_*, CFI_RESULT_*, CFI_WINDOW_SIZE
#include "regs/snooper_regs.h"  // CFG_REGS_* offsets and control bits

// ===========================================================================
// Compile-time knobs
// ===========================================================================
#ifndef VERBOSE
#define VERBOSE 1
#endif
#define LOG(fmt, ...) do { if (VERBOSE) printf(fmt, ##__VA_ARGS__); } while (0)

// Window anchor. 0 = syscall-anchored (default): last CFI_WINDOW_SIZE ring
// entries before the syscall. 1 = indirect-anchored (forward): scan the ring
// back to the most recent indirect (attacker-controllable) transfer and take
// the CFI_WINDOW_SIZE basic blocks executed right after it. A static loop
// between the hijacking transfer and the syscall adds no indirect transfers, so
// a fixed BB count anchored on the syscall can miss the attackable edge; this
// mode always reaches it. All-static history (no indirect found) => PASS/skip.
#ifndef WINDOW_ANCHOR_INDIRECT
#define WINDOW_ANCHOR_INDIRECT 1
#endif

// Run the pipeline per window; 0 = inspector only (fetch + PASS).
#ifndef ENABLE_PREPROCESS
#define ENABLE_PREPROCESS 1
#endif

// DMA the BB bytes into L1 (required by the pipeline).
#ifndef ENABLE_FETCH
#define ENABLE_FETCH 1
#endif
#if ENABLE_PREPROCESS && !ENABLE_FETCH
#error "ENABLE_PREPROCESS requires ENABLE_FETCH=1 (the pipeline needs the fetched BB bytes)"
#endif

// Run the dummy NN and drive the verdict; 0 = skip NN, always PASS.
#ifndef NN_INF_EN
#define NN_INF_EN 0
#endif

// Force VIOLATION every Nth syscall to test the kill path; 0 = never.
#ifndef NN_FLAG_EVERY
#define NN_FLAG_EVERY 0
#endif

// Dump the last fetched window to UART on exit (needs ENABLE_FETCH).
#ifndef DUMP_FETCH
#define DUMP_FETCH 1
#endif

// Read the ring window via iDMA burst instead of per-word AXI loads.
#ifndef DMA_RING_READ
#define DMA_RING_READ 1
#endif

// Collect cycle stats, print a summary on exit.
#ifndef ENABLE_STATS
#define ENABLE_STATS 1
#endif

// 1: per-stage timing (extra rdcycle overhead); 0: whole-call only.
#ifndef STATS_PER_PHASE
#define STATS_PER_PHASE 0
#endif
#define STATS_PHASE (ENABLE_STATS && STATS_PER_PHASE)

// Per-stage debug prints.
#ifndef CFI_DEBUG
#define CFI_DEBUG 0
#endif
#define DBG(fmt, ...) do { if (CFI_DEBUG) printf(fmt, ##__VA_ARGS__); } while (0)

// ===========================================================================
// Address map (cluster AXI master)
// ===========================================================================
#define BASE_SNPRCFG     0x15000000u   // snooper config registers
#define BASE_SNPR        0x16000000u   // snooper ring buffer
#define HOST_REGS_BASE   0x03000000u   // Cheshire host scratch registers

// Ring geometry (must match hardware)
#define SNPR_RING_BYTES  16380u
#define ENTRY_SIZE       20u           // 5 x 32-bit words per ring entry

#define L1_BUF_SIZE      4096u         // bytes reserved per window fetch slot

// ===========================================================================
// Preprocessing pipeline parameters (must match the cfi_preprocess reference)
// ===========================================================================
#define CFI_CHAIN_SLOTS  CFI_WINDOW_SIZE   // one pipeline slot per window entry
#define WINDOW_SIZE       32u
#define BLK0_TAIL         10u
#define BLK1_HEAD          5u
#define BLK1_TAIL          5u
#define BLK2_HEAD         10u
#define NOP32             0x00000013u
#define NUM_CORES          8u
#define ROWS_PER_CORE     (WINDOW_SIZE / NUM_CORES)  // = 4
#define MAX_IDX          128u   // max instructions indexed per slot

// The fixed window layout below assumes exactly three slots (blk0/blk1/blk2)
// and a 32-row window evenly split across 8 cores.
_Static_assert(CFI_WINDOW_SIZE == 3u,
               "window layout assumes exactly 3 basic-block slots");
_Static_assert((WINDOW_SIZE % NUM_CORES) == 0u,
               "WINDOW_SIZE must divide evenly across NUM_CORES");
_Static_assert(BLK0_TAIL + BLK1_HEAD + BLK1_TAIL + BLK2_HEAD <= WINDOW_SIZE,
               "window sections overflow WINDOW_SIZE");

// ===========================================================================
// One snooper ring entry.
// ===========================================================================
typedef struct {
    uint32_t pc_src_l;
    uint32_t pc_src_h;
    uint32_t pc_dst_l;
    uint32_t pc_dst_h;
    uint32_t ctr_type;
} snooper_entry_t;

// The DMA ring read lands raw ring bytes straight into cfi_window[].
_Static_assert(sizeof(snooper_entry_t) == ENTRY_SIZE,
               "snooper_entry_t must match the ring entry layout exactly");

// ===========================================================================
// PULP cluster cycle counters (custom CSRs, not standard mcycle).
// ===========================================================================
static inline void perf_init(void) {
    asm volatile ("csrw 0x79F, %0" :: "r"(0u)); // reset counters
    asm volatile ("csrw 0xCC0, %0" :: "r"(1u)); // count cycles
    asm volatile ("csrw 0xCC1, %0" :: "r"(1u)); // start counting
}

static inline uint32_t rdcycle(void) {
    uint32_t c;
    asm volatile ("csrr %0, 0x780" : "=r" (c));
    return c;
}

// ===========================================================================
// Hot-path buffers live in L1 (no data cache: every L2 touch is an AXI round-trip).
// ===========================================================================
snooper_entry_t *cfi_window;                   // last N entries before syscall (L1)
uint32_t         cfi_window_count;             // valid entries in cfi_window[]

#if WINDOW_ANCHOR_INDIRECT
static uint8_t  *ring_l1;                       // ring history DMA'd into L1 for the backward scan
#endif

#if ENABLE_FETCH
static uint32_t *fetch_bytes;                  // bytes DMA'd per slot (0 = none)
#endif

static cfi_va_pa_table_t *l1_tbl;              // VA->PA table copied into L1
static uint8_t           *l1_instr;            // CFI_WINDOW_SIZE fetch slots
static uint32_t va_start_l, va_start_h;
static uint32_t va_end_l,   va_end_h;

#if ENABLE_PREPROCESS
static uint32_t *l1_insn;                       // 32-instruction window (L1)
static uint8_t  *l1_feat;                       // 32x32 bit-feature matrix (L1)
static uint32_t *win_idx[CFI_CHAIN_SLOTS];      // per-slot byte-offset tables (L1)
static uint32_t  insn_count[CFI_CHAIN_SLOTS];   // instructions decoded per slot
static int       g_nn_result;                   // dummy NN verdict (core 0)
#endif

// Cross-core message state, published by core 0 before the release barrier.
static uint32_t g_msg_type;
static int      g_fatal;
static uint32_t g_snpr_ctrl;                    // armed snooper CTRL value (for ring reset)
#if WINDOW_ANCHOR_INDIRECT
static int      g_window_skip;                  // 1 = all-static window, skip the NN (PASS)
static uint32_t g_head_type;                    // entering indirect transfer's ctr_type
static uint32_t g_head_dst_l, g_head_dst_h;     // ... and its landing target (start of BB0)
#endif

#if ENABLE_STATS
// Cycle accumulators, summed over all syscalls (L1). Totals always measured;
// the per-stage fields below are filled only under STATS_PER_PHASE.
typedef struct {
    uint32_t syscall_count;
#if WINDOW_ANCHOR_INDIRECT
    uint32_t skip_count;          // all-static syscalls skipped (no indirect found)
    uint32_t ring_scan_cyc;       // ring-history DMA + backward L1 scan
#endif
    uint32_t inspect_total_cyc;
#if ENABLE_PREPROCESS
    uint32_t preprocess_total_cyc; // whole pipeline
#endif
    uint32_t last_read_cyc;       // snooper LAST read
    uint32_t ring_read_cyc;       // ring window DMA
    uint32_t va_to_pa_cyc;        // cfi_va_to_pa() scans
    uint32_t dma_cyc;             // BB fetch DMA
#if ENABLE_PREPROCESS
    uint32_t decode_cyc;
    uint32_t window_cyc;
    uint32_t explode_cyc;
    uint32_t nn_cyc;
#endif
} cfi_stats_t;
static cfi_stats_t *stat;
#endif

// inspect_window() context: L1 pointers + cached scalars, so the hot path
// never touches L2 globals.
typedef struct {
    snooper_entry_t   *win;                // == cfi_window
    uint32_t           vs_h, vs_l, ve_l;   // monitored VA range
#if WINDOW_ANCHOR_INDIRECT
    uint8_t           *ring;               // == ring_l1 (L1 scan buffer)
#endif
#if ENABLE_FETCH
    uint32_t          *fb;                 // == fetch_bytes
    uint8_t           *instr;              // == l1_instr
    cfi_va_pa_table_t *tbl;                // == l1_tbl
#endif
#if ENABLE_STATS
    cfi_stats_t       *st;                 // == stat
#endif
} inspect_ctx_t;

// ===========================================================================
// Low-level accessors
// ===========================================================================
static inline uint32_t snpr_cfg_rd(uint32_t off) {
    return *(volatile uint32_t *)(BASE_SNPRCFG + off);
}
static inline void snpr_cfg_wr(uint32_t off, uint32_t val) {
    *(volatile uint32_t *)(BASE_SNPRCFG + off) = val;
}
static inline uint32_t snpr_ring_rd(uint32_t off) {
    return *(volatile uint32_t *)(BASE_SNPR + off);
}
static inline uint32_t host_scratch_rd(uint32_t off) {
    return *(volatile uint32_t *)(HOST_REGS_BASE + off);
}
static inline void host_scratch_wr(uint32_t off, uint32_t val) {
    *(volatile uint32_t *)(HOST_REGS_BASE + off) = val;
}

// ===========================================================================
// Preprocessing helpers (from cfi_preprocess_standalone)
// ===========================================================================
#if ENABLE_PREPROCESS
// Byte-walk a slot, recording each instruction offset (RVC = 2 bytes, RV32 = 4).
// Returns the instruction count.
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

// Dummy stand-in for the trained CFI classifier: the reduction keeps a realistic
// compute cost (volatile acc defeats DCE), but always returns 0 = legitimate so
// the monitor never SIGKILLs the app. A real model replaces the return.
static int dummy_nn_inference(const uint8_t *feat, uint32_t rows, uint32_t cols)
{
    volatile uint32_t acc = 0;
    for (uint32_t r = 0; r < rows; r++)
        for (uint32_t c = 0; c < cols; c++)
            acc += (uint32_t)feat[r * cols + c] * (r * cols + c + 1u);
    (void)acc;
    return 0;   // 0 = legitimate (PASS); a trained model returns 1 on anomaly
}
#endif // ENABLE_PREPROCESS

// inspect_window — core 0: capture the CFI_WINDOW_SIZE window entries and
// DMA-fetch their basic blocks into L1. In the default (syscall-anchored) mode
// the window is the last N ring entries before the syscall; in the
// indirect-anchored mode (WINDOW_ANCHOR_INDIRECT) it is the N blocks executed
// right after the most recent indirect transfer. Sets *skip=1 when there is
// nothing to check (all-static history / no room). Returns the new drain_ptr.
static uint32_t inspect_window(const inspect_ctx_t *ctx, uint32_t drain_ptr, int *skip) {
    snooper_entry_t *win = ctx->win;
    *skip = 0;
#if WINDOW_ANCHOR_INDIRECT
    uint32_t head_dst_l = 0, head_dst_h = 0, head_type = 0;
#endif
#if ENABLE_FETCH
    uint32_t          *fb    = ctx->fb;
    uint8_t           *instr = ctx->instr;
    cfi_va_pa_table_t *tbl   = ctx->tbl;
#endif
#if ENABLE_STATS
    cfi_stats_t *st = ctx->st;
    uint32_t _t0 = rdcycle();
#if STATS_PER_PHASE
    uint32_t _s0, _s1;
    _s0 = rdcycle();
#endif
#endif

    uint32_t cur_last = snpr_cfg_rd(CFG_REGS_LAST_REG_OFFSET);
#if STATS_PHASE
    _s1 = rdcycle();
    st->last_read_cyc += _s1 - _s0;
#endif

    uint32_t n = 0;   // valid window entries (set per mode below)

#if WINDOW_ANCHOR_INDIRECT
    // ---- Indirect-anchored window ----------------------------------------
    // The bus exposes whole 20-byte records, so DMA the valid ring region into
    // L1 and inspect ctr_type there. Scan backward for the most recent indirect
    // (attacker-controllable) transfer, then take the CFI_WINDOW_SIZE blocks
    // executed right after it. All-static history (no indirect) => *skip (PASS).
    (void)drain_ptr;   // this mode bounds itself by [base, last), not the drain ptr
    uint32_t base = snpr_cfg_rd(CFG_REGS_BASE_REG_OFFSET);
    uint32_t avail_bytes = (cur_last >= base)
                         ? (cur_last - base)
                         : (SNPR_RING_BYTES - base + cur_last);
    if (avail_bytes > SNPR_RING_BYTES) avail_bytes = SNPR_RING_BYTES;
    uint32_t avail = avail_bytes / ENTRY_SIZE;

#if STATS_PHASE
    _s0 = rdcycle();
#endif
    // DMA [base, base+avail_bytes) into ring_l1 (oldest..newest, linear); a
    // wrapped region splits into two transfers on the ring boundary.
    if (avail_bytes > 0u) {
        uint32_t rl = (uint32_t)ctx->ring;
        if (base + avail_bytes <= SNPR_RING_BYTES) {
            plp_cl_dma_wait_toL1(
                pulp_cl_idma_L2ToL1(BASE_SNPR + base, rl,
                                    (unsigned short)avail_bytes));
        } else {
            uint32_t first  = SNPR_RING_BYTES - base;
            uint32_t second = avail_bytes - first;
            unsigned int id0 =
                pulp_cl_idma_L2ToL1(BASE_SNPR + base, rl,
                                    (unsigned short)first);
            unsigned int id1 =
                pulp_cl_idma_L2ToL1(BASE_SNPR, rl + first,
                                    (unsigned short)second);
            plp_cl_dma_wait_toL1(id0);
            plp_cl_dma_wait_toL1(id1);
        }
    }

    // Backward scan for the newest indirect transfer.
    const snooper_entry_t *e = (const snooper_entry_t *)ctx->ring;
    int found = -1;
    for (int k = (int)avail - 1; k >= 0; k--) {
        if (cfi_ctr_is_indirect(e[k].ctr_type)) { found = k; break; }
    }
#if STATS_PHASE
    _s1 = rdcycle();
    st->ring_scan_cyc += _s1 - _s0;
#endif

    if (found < 0) {
        *skip = 1;   // no attackable edge in the trace -> safe to skip the NN
    } else {
        head_dst_l = e[found].pc_dst_l;
        head_dst_h = e[found].pc_dst_h;
        head_type  = e[found].ctr_type;

        // Blocks after the indirect, up to CFI_WINDOW_SIZE.
        uint32_t entries_after = (avail - 1u) - (uint32_t)found;
        n = (entries_after < CFI_WINDOW_SIZE) ? entries_after
                                              : (uint32_t)CFI_WINDOW_SIZE;
        if (n == 0u) {
            *skip = 1;   // indirect immediately precedes the syscall: no block after it
        } else {
            for (uint32_t i = 0; i < n; i++)
                win[i] = e[found + 1 + (int)i];
        }
    }
    cfi_window_count = n;

    // Publish the entering indirect transfer (dump / future NN feature).
    g_head_type  = head_type;
    g_head_dst_l = head_dst_l;
    g_head_dst_h = head_dst_h;
#else
    // ---- Syscall-anchored window (default) -------------------------------
    // New ring bytes since the last drain.
    uint32_t depth;
    if (cur_last >= drain_ptr)
        depth = cur_last - drain_ptr;
    else
        depth = SNPR_RING_BYTES - drain_ptr + cur_last;
    if (depth > SNPR_RING_BYTES)
        depth = SNPR_RING_BYTES;

    // Capture the last min(available, WINDOW_SIZE) entries.
    uint32_t avail     = depth / ENTRY_SIZE;
    n                  = (avail < CFI_WINDOW_SIZE) ? avail : (uint32_t)CFI_WINDOW_SIZE;
    uint32_t use_bytes = n * ENTRY_SIZE;

    // Window start = (cur_last - use_bytes) mod RING_BYTES.
    uint32_t rptr;
    if (cur_last >= use_bytes)
        rptr = cur_last - use_bytes;
    else
        rptr = SNPR_RING_BYTES - (use_bytes - cur_last);

#ifndef DEBUG_PTR
#define DEBUG_PTR 0
#endif
#if DEBUG_PTR
    {
        static uint32_t dbg_prev = 0, dbg_n = 0;
        uint32_t base = snpr_cfg_rd(CFG_REGS_BASE_REG_OFFSET);
        uint32_t ctrl = snpr_cfg_rd(CFG_REGS_CTRL_REG_OFFSET);
        uint32_t dl   = (cur_last >= dbg_prev)
                        ? (cur_last - dbg_prev)
                        : (SNPR_RING_BYTES - dbg_prev + cur_last);
        LOG("[cl-insp] #%u last=0x%x base=0x%x dl=%u depth=%u n=%u rptr=0x%x ctrl=0x%08x\r\n",
            (unsigned)dbg_n, (unsigned)cur_last, (unsigned)base, (unsigned)dl,
            (unsigned)depth, (unsigned)n, (unsigned)rptr, (unsigned)ctrl);
        dbg_prev = cur_last; dbg_n++;
    }
#endif

#if STATS_PHASE
    _s0 = rdcycle();
#endif
#if DMA_RING_READ
    // Burst the window into cfi_window[]; a wrapped window splits into two
    // transfers on an entry boundary.
    if (n > 0) {
        uint32_t wdst = (uint32_t)win;
        if (rptr + use_bytes <= SNPR_RING_BYTES) {
            plp_cl_dma_wait_toL1(
                pulp_cl_idma_L2ToL1(BASE_SNPR + rptr, wdst,
                                    (unsigned short)use_bytes));
        } else {
            uint32_t first  = SNPR_RING_BYTES - rptr;
            uint32_t second = use_bytes - first;
            unsigned int id0 =
                pulp_cl_idma_L2ToL1(BASE_SNPR + rptr, wdst,
                                    (unsigned short)first);
            unsigned int id1 =
                pulp_cl_idma_L2ToL1(BASE_SNPR, wdst + first,
                                    (unsigned short)second);
            plp_cl_dma_wait_toL1(id0);
            plp_cl_dma_wait_toL1(id1);
        }
    }
    cfi_window_count = n;
#else
    for (uint32_t i = 0; i < n; i++) {
        win[i].pc_src_l = snpr_ring_rd(rptr + 0x00);
        win[i].pc_src_h = snpr_ring_rd(rptr + 0x04);
        win[i].pc_dst_l = snpr_ring_rd(rptr + 0x08);
        win[i].pc_dst_h = snpr_ring_rd(rptr + 0x0C);
        win[i].ctr_type = snpr_ring_rd(rptr + 0x10);
        rptr += ENTRY_SIZE;
        if (rptr >= SNPR_RING_BYTES) rptr -= SNPR_RING_BYTES;
    }
    cfi_window_count = n;
#endif
#if STATS_PHASE
    _s1 = rdcycle();
    st->ring_read_cyc += _s1 - _s0;
#endif
#endif  // WINDOW_ANCHOR_INDIRECT

#if ENABLE_FETCH
    // Fetch each BB: start = prev pc_dst (or pc_src if out of range), end =
    // pc_src + 4; VA -> PA before the DMA. Zero ALL slots so a shorter window
    // this call leaves no stale byte count for the pipeline.
    for (uint32_t i = 0; i < CFI_WINDOW_SIZE; i++) fb[i] = 0u;

    uint32_t vs_h = ctx->vs_h, vs_l = ctx->vs_l, ve_l = ctx->ve_l;

    // Issue all BB transfers up front (distinct L1 slots, no alias), drain once.
    unsigned int last_id = 0;
    int have_last = 0;

    for (uint32_t i = 0; i < n; i++) {
#if WINDOW_ANCHOR_INDIRECT
        // Slot 0's block starts where the anchoring indirect transfer landed.
        uint32_t bb_va_l = (i > 0) ? win[i - 1].pc_dst_l : head_dst_l;
        uint32_t bb_va_h = (i > 0) ? win[i - 1].pc_dst_h : head_dst_h;
#else
        uint32_t bb_va_l = (i > 0) ? win[i - 1].pc_dst_l : win[i].pc_src_l;
        uint32_t bb_va_h = (i > 0) ? win[i - 1].pc_dst_h : win[i].pc_src_h;
#endif

        if (bb_va_h != vs_h ||
            bb_va_l  < vs_l ||
            bb_va_l >= ve_l) {
            bb_va_l = win[i].pc_src_l;
            bb_va_h = win[i].pc_src_h;
        }

#if STATS_PHASE
        _s0 = rdcycle();
#endif
        uint32_t bb_pa = cfi_va_to_pa(tbl, bb_va_h, bb_va_l);
#if STATS_PHASE
        _s1 = rdcycle();
        st->va_to_pa_cyc += _s1 - _s0;
#endif
        if (bb_pa == 0u)
            continue;   // fb[i] stays 0

        uint32_t bytes;
        if (win[i].pc_src_h != bb_va_h ||
            win[i].pc_src_l  < bb_va_l) {
            bytes = 4u;   // guard against underflow on a backward branch
        } else {
            bytes = (win[i].pc_src_l - bb_va_l) + 4u;
            if (bytes > L1_BUF_SIZE) bytes = L1_BUF_SIZE;
        }
        fb[i] = bytes;

        uint32_t dst = (uint32_t)instr + i * L1_BUF_SIZE;

#if STATS_PHASE
        _s0 = rdcycle();
#endif
        last_id   = pulp_cl_idma_L2ToL1(bb_pa, dst, (unsigned short)bytes);
        have_last = 1;
#if STATS_PHASE
        _s1 = rdcycle();
        st->dma_cyc += _s1 - _s0;
#endif
    }

    // iDMA tx ids complete in order, so waiting the last implies all landed.
#if STATS_PHASE
    _s0 = rdcycle();
#endif
    if (have_last)
        plp_cl_dma_wait_toL1(last_id);
#if STATS_PHASE
    _s1 = rdcycle();
    st->dma_cyc += _s1 - _s0;
#endif
#endif  // ENABLE_FETCH

#if ENABLE_STATS
    st->inspect_total_cyc += rdcycle() - _t0;
    st->syscall_count++;
#endif
    return cur_last;
}

// ===========================================================================
// preprocess_pipeline — all 8 cores (internal barriers): fetched BBs
// (l1_instr/fetch_bytes, cfi_window_count valid slots) -> 32x32 feature matrix
// l1_feat -> dummy NN verdict g_nn_result.
// ===========================================================================
#if ENABLE_PREPROCESS
static void preprocess_pipeline(uint32_t core_id) {
    uint32_t n = cfi_window_count;   // 0..CFI_WINDOW_SIZE

#if ENABLE_STATS
    uint32_t _pt0 = 0;               // whole-pipeline bracket (general stat)
    if (core_id == 0) _pt0 = rdcycle();
#endif
#if STATS_PHASE
    uint32_t t0 = 0;                 // per-stage bracket (detailed stats only)
    if (core_id == 0) t0 = rdcycle();
#endif

    // ---- Stage DECODE — one core per slot (cores 0..CFI_CHAIN_SLOTS-1) -----
    if (core_id < CFI_CHAIN_SLOTS) {
        uint32_t bytes = (core_id < n) ? fetch_bytes[core_id] : 0u;
        const uint8_t *buf = l1_instr + core_id * L1_BUF_SIZE;
        insn_count[core_id] = (bytes > 0u)
            ? decode_and_index(buf, bytes, win_idx[core_id])
            : 0u;
    }
    synch_barrier();  // insn_count[] visible to all cores
#if STATS_PHASE
    if (core_id == 0) { stat->decode_cyc += rdcycle() - t0; t0 = rdcycle(); }
#endif

    // ---- Stage WINDOW BUILD — layout: blk0 tail[0..10) blk1 head[10..15) -----
    // blk1 tail[15..20) blk2 head[20..30) NOP pad[30..32).
    // core 0 pre-fills NOP32; all 8 cores then fill their rows in parallel.
    if (core_id == 0)
        for (uint32_t j = 0; j < WINDOW_SIZE; j++) l1_insn[j] = NOP32;
    synch_barrier();  // NOP32 pre-fill visible before the parallel fill
    {
        const uint32_t S1 = BLK0_TAIL;               //  0..10 -> blk0 tail
        const uint32_t S2 = BLK0_TAIL + BLK1_HEAD;   // 10..15 -> blk1 head
        const uint32_t S3 = S2 + BLK1_TAIL;          // 15..20 -> blk1 tail
        const uint32_t S4 = S3 + BLK2_HEAD;          // 20..30 -> blk2 head

        uint32_t n0 = insn_count[0], n1 = insn_count[1], n2 = insn_count[2];
        const uint8_t *b0 = l1_instr + 0u * L1_BUF_SIZE;
        const uint8_t *b1 = l1_instr + 1u * L1_BUF_SIZE;
        const uint8_t *b2 = l1_instr + 2u * L1_BUF_SIZE;

        uint32_t row_start = core_id * ROWS_PER_CORE;
        uint32_t row_end   = row_start + ROWS_PER_CORE;

        for (uint32_t wi = row_start; wi < row_end; wi++) {
            uint32_t insn = NOP32;
            if (wi < S1) {
                uint32_t skip = (n0 > BLK0_TAIL) ? (n0 - BLK0_TAIL) : 0u;
                uint32_t i = skip + wi;
                if (i < n0) insn = read_insn(b0, win_idx[0][i]);
            } else if (wi < S2) {
                uint32_t i = wi - S1;
                if (i < n1) insn = read_insn(b1, win_idx[1][i]);
            } else if (wi < S3) {
                uint32_t skip = (n1 > BLK1_TAIL) ? (n1 - BLK1_TAIL) : 0u;
                uint32_t i = skip + (wi - S2);
                if (i < n1) insn = read_insn(b1, win_idx[1][i]);
            } else if (wi < S4) {
                uint32_t i = wi - S3;
                if (i < n2) insn = read_insn(b2, win_idx[2][i]);
            }
            l1_insn[wi] = insn;
        }
    }
    synch_barrier();  // l1_insn[] complete
#if STATS_PHASE
    if (core_id == 0) { stat->window_cyc += rdcycle() - t0; t0 = rdcycle(); }
#endif

    // ---- Stage BIT-EXPLODE — parallel across 8 cores ----------------------
    {
        uint32_t row_start = core_id * ROWS_PER_CORE;
        uint32_t row_end   = row_start + ROWS_PER_CORE;
        for (uint32_t r = row_start; r < row_end; r++)
            explode_insn(l1_insn[r], &l1_feat[r * 32u]);
    }
    synch_barrier();  // l1_feat[] complete
#if STATS_PHASE
    if (core_id == 0) { stat->explode_cyc += rdcycle() - t0; t0 = rdcycle(); }
#endif

    // ---- Stage NN INFERENCE — core 0 -------------------------------------
    if (core_id == 0) {
#if NN_INF_EN
        g_nn_result = dummy_nn_inference(l1_feat, WINDOW_SIZE, 32u);
#else
        g_nn_result = 0;
#endif
#if STATS_PHASE
        stat->nn_cyc += rdcycle() - t0;
#endif
        DBG("[dbg:NN] n=%u nn=%d\r\n", (unsigned)n, g_nn_result);
    }

#if ENABLE_STATS
    // General stat: whole-pipeline wall-clock (core 0 view, incl. barrier waits).
    if (core_id == 0) stat->preprocess_total_cyc += rdcycle() - _pt0;
#endif
    // No trailing barrier: the caller's iteration barrier syncs cores; g_nn_result
    // is core-0 only.
}
#endif // ENABLE_PREPROCESS

// dump_last_window — print the last window (entries + decoded instructions).
#if DUMP_FETCH && ENABLE_FETCH
static void dump_last_window(void) {
    LOG("[cl-insp] === fetch dump: %u entries ===\r\n",
        (unsigned)cfi_window_count);
#if WINDOW_ANCHOR_INDIRECT
    // The window is anchored on the most recent indirect transfer; BB0 starts at
    // its landing target. cfi_window_count == 0 here means an all-static window.
    LOG("[cl-insp] anchor: indirect type=%u dst=0x%x_%08x%s\r\n",
        (unsigned)g_head_type, (unsigned)g_head_dst_h, (unsigned)g_head_dst_l,
        (cfi_window_count == 0u) ? "  (all-static -> skipped)" : "");
#endif
    for (uint32_t i = 0; i < cfi_window_count; i++) {
        LOG("[cl-insp] [%u] src=0x%x_%08x  dst=0x%x_%08x  type=%u\r\n",
            (unsigned)i,
            (unsigned)cfi_window[i].pc_src_h,
            (unsigned)cfi_window[i].pc_src_l,
            (unsigned)cfi_window[i].pc_dst_h,
            (unsigned)cfi_window[i].pc_dst_l,
            (unsigned)cfi_window[i].ctr_type);

        uint32_t nb = fetch_bytes[i];
        if (nb == 0u) {
            LOG("[cl-insp]      (no fetch)\r\n");
            continue;
        }

        uint32_t base = (uint32_t)l1_instr + i * L1_BUF_SIZE;
        uint32_t byte_off = 0u;
        while (byte_off < nb) {
            uint16_t hw0 = *(volatile uint16_t *)(base + byte_off);
            if ((hw0 & 0x3u) == 0x3u) {
                uint16_t hw1   = *(volatile uint16_t *)(base + byte_off + 2u);
                uint32_t instr = (uint32_t)hw0 | ((uint32_t)hw1 << 16);
                LOG("[cl-insp]      +0x%03x: 0x%08x\r\n",
                    (unsigned)byte_off, (unsigned)instr);
                byte_off += 4u;
            } else {
                LOG("[cl-insp]      +0x%03x: 0x%04x (RVC)\r\n",
                    (unsigned)byte_off, (unsigned)hw0);
                byte_off += 2u;
            }
        }
    }
#if ENABLE_PREPROCESS
    LOG("[cl-insp] --- 32-insn window (last syscall) ---\r\n");
    for (uint32_t j = 0; j < WINDOW_SIZE; j++)
        LOG("[cl-insp]   win[%2u] 0x%08x%s\r\n",
            (unsigned)j, (unsigned)l1_insn[j],
            (l1_insn[j] == NOP32) ? " (NOP/pad)" : "");
    LOG("[cl-insp]   dummy NN verdict = %d (%s)\r\n",
        g_nn_result, g_nn_result ? "VIOLATION" : "PASS");
#endif
    LOG("[cl-insp] === end dump ===\r\n");
}
#endif

// ===========================================================================
// arm_snooper — RANGE_2 = monitored VA range, U mode, halt disabled.
// ===========================================================================
static void arm_snooper(void) {
    snpr_cfg_wr(CFG_REGS_CTRL_REG_OFFSET, 0u);

    // Clear ring ptr / full-latch so each run records into a fresh buffer.
    snpr_cfg_wr(CFG_REGS_CTRL_REG_OFFSET, (1u << CFG_REGS_CTRL_CNT_RST_BIT));
    snpr_cfg_wr(CFG_REGS_CTRL_REG_OFFSET, 0u);

    snpr_cfg_wr(CFG_REGS_RANGE_2_BASE_H_REG_OFFSET, va_start_h);
    snpr_cfg_wr(CFG_REGS_RANGE_2_BASE_L_REG_OFFSET, va_start_l);
    snpr_cfg_wr(CFG_REGS_RANGE_2_LAST_H_REG_OFFSET, va_end_h);
    snpr_cfg_wr(CFG_REGS_RANGE_2_LAST_L_REG_OFFSET, va_end_l);

    // HALT_LEVEL above ring size: the ring wraps silently, never halts.
    snpr_cfg_wr(CFG_REGS_HALT_LEVEL_REG_OFFSET, 0x0000FFFFu);

    // U mode + RANGE_2 filter; trace mode = ADDRESS (0): 20-byte records.
    const uint32_t TRACE_MODE_ADDRESS = 0u;
    uint32_t ctrl = (1u << CFG_REGS_CTRL_U_MODE_BIT) |
                    (1u << CFG_REGS_CTRL_M_MODE_BIT) |
                    (1u << CFG_REGS_CTRL_PC_RANGE_2_BIT) |
                    ((TRACE_MODE_ADDRESS & CFG_REGS_CTRL_TRACE_MODE_MASK)
                        << CFG_REGS_CTRL_TRACE_MODE_OFFSET);
    snpr_cfg_wr(CFG_REGS_CTRL_REG_OFFSET, ctrl);
    g_snpr_ctrl = ctrl;   // remember the armed value for per-syscall ring resets

    uint32_t ctrl_rb = snpr_cfg_rd(CFG_REGS_CTRL_REG_OFFSET);
    LOG("[cl-insp] snooper CTRL=0x%08x (want 0x%08x) base=0x%x last=0x%x\r\n",
        (unsigned)ctrl_rb, (unsigned)ctrl,
        (unsigned)snpr_cfg_rd(CFG_REGS_BASE_REG_OFFSET),
        (unsigned)snpr_cfg_rd(CFG_REGS_LAST_REG_OFFSET));
}

// reset_snooper_ring — empty the ring, keeping the snooper armed. Only safe when
// the monitored core is quiesced (no in-range transfers in flight); we call it
// per syscall while CVA6 is blocked on the BUSY sentinel. CNT_RST clears the
// ring pointer + full-latch; the RANGE_2 / HALT config persists across the CTRL
// writes. The trailing fence orders the re-arm before the verdict write that
// releases CVA6, so the next burst is never recorded into a disarmed snooper.
static void reset_snooper_ring(void) {
    snpr_cfg_wr(CFG_REGS_CTRL_REG_OFFSET, 0u);
    snpr_cfg_wr(CFG_REGS_CTRL_REG_OFFSET, (1u << CFG_REGS_CTRL_CNT_RST_BIT));
    snpr_cfg_wr(CFG_REGS_CTRL_REG_OFFSET, 0u);
    snpr_cfg_wr(CFG_REGS_CTRL_REG_OFFSET, g_snpr_ctrl);
    asm volatile("fence" ::: "memory");
}

static void disarm_snooper(void) {
    snpr_cfg_wr(CFG_REGS_CTRL_REG_OFFSET, 0u);
    snpr_cfg_wr(CFG_REGS_CTRL_REG_OFFSET, (1u << CFG_REGS_CTRL_CNT_RST_BIT));
    snpr_cfg_wr(CFG_REGS_CTRL_REG_OFFSET, 0u);
}

// ===========================================================================
// setup_core0 — core 0: L1 buffers, VA->PA table, snooper, mailbox, ack Ibex.
// Sets g_fatal on error. Cores 1..7 wait at the post-setup barrier in run_cfi().
// ===========================================================================
static void setup_core0(void) {
    perf_init();
    LOG("[cl-insp] cfi_syscall_cluster_preprocess: start\r\n");

    // 1. Allocate L1 buffers.
    l1_tbl     = (cfi_va_pa_table_t *)pi_l1_malloc(0, sizeof(cfi_va_pa_table_t));
    l1_instr   = (uint8_t *)pi_l1_malloc(0, CFI_WINDOW_SIZE * L1_BUF_SIZE);
    cfi_window = (snooper_entry_t *)pi_l1_malloc(0, CFI_WINDOW_SIZE * sizeof(snooper_entry_t));
#if WINDOW_ANCHOR_INDIRECT
    ring_l1 = (uint8_t *)pi_l1_malloc(0, SNPR_RING_BYTES);
#endif
#if ENABLE_FETCH
    fetch_bytes = (uint32_t *)pi_l1_malloc(0, CFI_WINDOW_SIZE * sizeof(uint32_t));
#endif
#if ENABLE_PREPROCESS
    l1_insn = (uint32_t *)pi_l1_malloc(0, WINDOW_SIZE * sizeof(uint32_t));
    l1_feat = (uint8_t  *)pi_l1_malloc(0, WINDOW_SIZE * 32u);
    for (uint32_t k = 0; k < CFI_CHAIN_SLOTS; k++)
        win_idx[k] = (uint32_t *)pi_l1_malloc(0, MAX_IDX * sizeof(uint32_t));
#endif
#if ENABLE_STATS
    stat = (cfi_stats_t *)pi_l1_malloc(0, sizeof(cfi_stats_t));
    if (stat) *stat = (cfi_stats_t){0};   // pi_l1_malloc does not zero
#endif

    int alloc_ok = (l1_tbl && l1_instr && cfi_window);
#if WINDOW_ANCHOR_INDIRECT
    alloc_ok = alloc_ok && ring_l1;
#endif
#if ENABLE_FETCH
    alloc_ok = alloc_ok && fetch_bytes;
#endif
#if ENABLE_PREPROCESS
    alloc_ok = alloc_ok && l1_insn && l1_feat;
    for (uint32_t k = 0; k < CFI_CHAIN_SLOTS; k++) alloc_ok = alloc_ok && win_idx[k];
#endif
#if ENABLE_STATS
    alloc_ok = alloc_ok && stat;
#endif
    if (!alloc_ok) {
        LOG("[cl-insp] ERROR: L1 alloc failed\r\n");
        g_fatal = 1;
        return;
    }

    // 2. Wait for the VA->PA table (scratch 12), DMA it into L1, validate.
    while (host_scratch_rd(CFI_SCRATCH_LAUNCHER_OFF) != SYNC_TABLE_PUBLISHED)
        ;
    host_scratch_wr(CFI_SCRATCH_LAUNCHER_OFF, 0u);

    plp_idma_enable_clk();
    plp_cl_dma_wait_toL1(
        pulp_cl_idma_L2ToL1(CFI_TABLE_PHYS_BASE,
                            (uint32_t)l1_tbl,
                            (unsigned short)sizeof(cfi_va_pa_table_t)));
    plp_idma_disable_clk();

    if (l1_tbl->ready != CFI_TABLE_READY_MAGIC ||
        l1_tbl->magic != CFI_TABLE_MAGIC) {
        LOG("[cl-insp] ERROR: bad CFI table (magic=0x%08x ready=0x%08x)\r\n",
            (unsigned)l1_tbl->magic, (unsigned)l1_tbl->ready);
        g_fatal = 1;
        return;
    }

    va_start_l = l1_tbl->va_range_start_l;
    va_start_h = l1_tbl->va_range_start_h;
    va_end_l   = l1_tbl->va_range_end_l;
    va_end_h   = l1_tbl->va_range_end_h;
    LOG("[cl-insp] VA range 0x%x_%08x - 0x%x_%08x  segs=%u\r\n",
        (unsigned)va_start_h, (unsigned)va_start_l,
        (unsigned)va_end_h,   (unsigned)va_end_l,
        (unsigned)l1_tbl->num_segs);

    // 3. Configure the snooper (ARM_SNOOPER=0 skips it, for diagnostics).
#ifndef ARM_SNOOPER
#define ARM_SNOOPER 1
#endif
#if ARM_SNOOPER
    arm_snooper();
#else
    LOG("[cl-insp] arm_snooper SKIPPED (ARM_SNOOPER=0 diagnostic)\r\n");
#endif

    // 4. Arm the SECD mailbox RCV path, then ack CVA6 (scratch 13).
    hal_mailboxes_clear_receive_irq();
    hal_mailboxes_enable_receive_irq();

    host_scratch_wr(CFI_SCRATCH_IBEX_OFF, SYNC_IBEX_CFI_READY);
}

// ===========================================================================
// run_cfi — all 8 cores. Core 0 owns setup/mailbox/snooper; a barrier loop
// releases all cores to run the pipeline on each fetched window.
// ===========================================================================
static int run_cfi(uint32_t core_id) {
    // -- Setup: core 0 only; everyone rendezvous afterwards ------------------
    if (core_id == 0)
        setup_core0();

    synch_barrier();               // buffers + handshake complete (or g_fatal)

    if (g_fatal) {
        if (core_id == 0) {
            hal_mailboxes_write_return_value(1);
            hal_mailboxes_ring_doorbell();
        }
        return 1;
    }

    // -- Build the inspect_window context once (core 0 uses it) --------------
    inspect_ctx_t ctx;
    ctx.win  = cfi_window;
    ctx.vs_h = va_start_h;
    ctx.vs_l = va_start_l;
    ctx.ve_l = va_end_l;
#if WINDOW_ANCHOR_INDIRECT
    ctx.ring = ring_l1;
#endif
#if ENABLE_FETCH
    ctx.fb    = fetch_bytes;
    ctx.instr = l1_instr;
    ctx.tbl   = l1_tbl;
#endif
#if ENABLE_STATS
    ctx.st = stat;
#endif

    // iDMA clock stays on for the whole loop (per-call gating was overhead).
    if (core_id == 0)
        plp_idma_enable_clk();

    uint32_t drain_ptr = 0;   // core 0 software ring read pointer
    uint32_t syscalls  = 0;   // core 0 processed-syscall counter

    // -- Steady-state loop: one iteration per Ibex mailbox kick --------------
    for (;;) {
        if (core_id == 0) {
            while (hal_mailboxes_receive_irq_pending() == 0)
                ;
            uint32_t msg = (uint32_t)hal_mailboxes_read_letter0();
            hal_mailboxes_clear_receive_irq();

            g_msg_type = msg & CFI_MSG_TYPE_MASK;

            if (g_msg_type == CFI_MSG_APP_DONE) {
                disarm_snooper();
                LOG("[cl-insp] app done\r\n");
            } else if (g_msg_type == CFI_MSG_SYSCALL) {
                uint32_t nr = msg & CFI_MSG_SYSCALL_NR_MASK;
                (void)nr;   // available for future per-syscall policy
                int skip = 0;
                drain_ptr = inspect_window(&ctx, drain_ptr, &skip);
#if WINDOW_ANCHOR_INDIRECT
                g_window_skip = skip;   // published before the release barrier
#else
                (void)skip;
#endif
            } else {
                LOG("[cl-insp] unknown msg 0x%08x\r\n", (unsigned)msg);
            }
        }

        synch_barrier();   // g_msg_type + fetched BBs visible to all cores

        if (g_msg_type == CFI_MSG_APP_DONE)
            break;         // all cores leave together (shared g_msg_type)

        if (g_msg_type == CFI_MSG_SYSCALL) {
            uint32_t verdict = CFI_RESULT_PASS;
#if WINDOW_ANCHOR_INDIRECT
            if (g_window_skip) {
                // All-static window (no indirect transfer in the trace): no
                // attackable edge, so PASS and skip the NN entirely. All cores
                // branch on the shared g_window_skip, so the barrier stays in sync.
#if ENABLE_STATS
                if (core_id == 0) stat->skip_count++;
#endif
            } else
#endif
            {
#if ENABLE_PREPROCESS
                preprocess_pipeline(core_id);       // all 8 cores, internal barriers
                if (core_id == 0) {
                    verdict = g_nn_result ? CFI_RESULT_VIOLATION : CFI_RESULT_PASS;
#if NN_FLAG_EVERY
                    if (((syscalls + 1u) % (uint32_t)(NN_FLAG_EVERY)) == 0u)
                        verdict = CFI_RESULT_VIOLATION;
#endif
                }
#endif
            }

            // Core 0: computation done and CVA6 is still blocked on this syscall's
            // BUSY sentinel (no in-range transfers in flight), so empty the ring
            // now -- the next syscall then records into a fresh, un-wrapped ring.
            // Reset the SW drain pointer to match, then release CVA6 with the
            // verdict (reset_snooper_ring() fences the re-arm ahead of this write).
            if (core_id == 0) {
                reset_snooper_ring();
                drain_ptr = 0;
                host_scratch_wr(CFI_SCRATCH_RESULT_OFF, verdict);
                syscalls++;
            }
        } else {
            // Unknown message: never stall CVA6.
            if (core_id == 0)
                host_scratch_wr(CFI_SCRATCH_RESULT_OFF, CFI_RESULT_PASS);
        }

        synch_barrier();   // iteration complete
    }

    // -- Teardown: core 0 only ----------------------------------------------
    if (core_id == 0) {
        plp_idma_disable_clk();

#if ENABLE_STATS
        {
            uint32_t nn        = stat->syscall_count ? stat->syscall_count : 1u;
            uint32_t avg_insp  = stat->inspect_total_cyc / nn;
#if ENABLE_PREPROCESS
            uint32_t avg_pre   = stat->preprocess_total_cyc / nn;
            uint32_t avg_total = avg_insp + avg_pre;
#else
            uint32_t avg_total = avg_insp;
#endif
            LOG("[cl-insp] stats over %u syscalls (window=%u):\r\n",
                (unsigned)stat->syscall_count, (unsigned)CFI_WINDOW_SIZE);
#if WINDOW_ANCHOR_INDIRECT
            LOG("[cl-insp]   all-static syscalls skipped: %u/%u\r\n",
                (unsigned)stat->skip_count, (unsigned)stat->syscall_count);
#endif
#if STATS_PER_PHASE
            LOG("[cl-insp]   last_read     avg=%5u cyc\r\n", (unsigned)(stat->last_read_cyc / nn));
            LOG("[cl-insp]   ring_read     avg=%5u cyc\r\n", (unsigned)(stat->ring_read_cyc / nn));
#if WINDOW_ANCHOR_INDIRECT
            LOG("[cl-insp]   ring_scan     avg=%5u cyc\r\n", (unsigned)(stat->ring_scan_cyc / nn));
#endif
            LOG("[cl-insp]   va_to_pa      avg=%5u cyc\r\n", (unsigned)(stat->va_to_pa_cyc  / nn));
            LOG("[cl-insp]   dma_fetch     avg=%5u cyc\r\n", (unsigned)(stat->dma_cyc       / nn));
#endif
            LOG("[cl-insp]   inspect_total avg=%5u cyc\r\n", (unsigned)avg_insp);
#if ENABLE_PREPROCESS
#if STATS_PER_PHASE
            LOG("[cl-insp]   decode        avg=%5u cyc\r\n", (unsigned)(stat->decode_cyc  / nn));
            LOG("[cl-insp]   window        avg=%5u cyc\r\n", (unsigned)(stat->window_cyc  / nn));
            LOG("[cl-insp]   explode       avg=%5u cyc\r\n", (unsigned)(stat->explode_cyc / nn));
            LOG("[cl-insp]   nn            avg=%5u cyc\r\n", (unsigned)(stat->nn_cyc      / nn));
#endif
            LOG("[cl-insp]   preprocess    avg=%5u cyc\r\n", (unsigned)avg_pre);
#endif
            LOG("[cl-insp]   TOTAL         avg=%5u cyc  (inspect + preprocess)\r\n",
                (unsigned)avg_total);
        }
#endif

#if DUMP_FETCH && ENABLE_FETCH
        dump_last_window();
#endif

        LOG("[cl-insp] done: syscalls=%u\r\n", (unsigned)syscalls);

        // Release the Ibex firmware (SECD SND doorbell + return value).
        hal_mailboxes_write_return_value(0);
        hal_mailboxes_ring_doorbell();
    }

    return 0;
}

// ===========================================================================
// main — all cores enter; core 0 drives, cores 1..7 are pipeline workers.
// ===========================================================================
int main(void) {
    uint32_t core_id = (uint32_t)rt_core_id();
    if(core_id == 0){
        LOG("[cl-insp] start");
    }

    int ret = run_cfi(core_id);

    synch_barrier();
    return ret;
}
