/*
 * Copyright 2026 Fondazione Chips-IT.
 * Licensed under the Apache License, Version 2.0, see LICENSE for details.
 * SPDX-License-Identifier: Apache-2.0
 *
 * cfi_syscall_cluster/test.c — cluster port of the syscall-triggered CFI
 * window inspector (sw/tests/scarv/cfi_syscall_inspector.c).  Core 0 does
 * all the work (VA->PA table, snooper config, ring inspection, BB fetch);
 * Ibex only forwards CVA6 syscall doorbells through the SECD mailbox.
 */

#include <stdio.h>
#include <stdint.h>
#include "pulp.h"

#include "cfi_va_pa_table.h"    // VA->PA table + scratch 12/13 protocol
#include "cfi_syscall_proto.h"  // CFI_MSG_*, CFI_RESULT_*, CFI_WINDOW_SIZE
#include "regs/snooper_regs.h"  // CFG_REGS_* offsets and control bits

// Compile-time knobs
#ifndef VERBOSE
#define VERBOSE 1
#endif
#define LOG(fmt, ...) do { if (VERBOSE) printf(fmt, ##__VA_ARGS__); } while (0)

// DMA-fetch the basic-block bytes of every window entry into L1.
#ifndef ENABLE_FETCH
#define ENABLE_FETCH 1
#endif

// Dump the last fetched window to UART after the app exits (needs ENABLE_FETCH).
#ifndef DUMP_FETCH
#define DUMP_FETCH 1
#endif

// Read the ring window with iDMA bursts instead of per-word AXI loads.
#ifndef DMA_RING_READ
#define DMA_RING_READ 1
#endif

// Collect cycle statistics and print a summary after the app exits.
#ifndef ENABLE_STATS
#define ENABLE_STATS 1
#endif

// 1: time each phase (adds rdcycle overhead); 0: whole-call timing only.
#ifndef STATS_PER_PHASE
#define STATS_PER_PHASE 0
#endif
#define STATS_PHASE (ENABLE_STATS && STATS_PER_PHASE)

// Address map (cluster AXI master)
#define BASE_SNPRCFG     0x15000000u   // snooper config registers
#define BASE_SNPR        0x16000000u   // snooper ring buffer
#define HOST_REGS_BASE   0x03000000u   // Cheshire host scratch registers

// Ring geometry (must match hardware)
#define SNPR_RING_BYTES  16380u
#define ENTRY_SIZE       20u           // 5 x 32-bit words per ring entry

#define L1_BUF_SIZE      4096u         // bytes reserved per window fetch slot

// One snooper ring entry.
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

// PULP cluster cycle counters (custom CSRs, not standard mcycle).
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

// Hot-path buffers live in L1: the cluster core has no data cache, so every
// L2 global touch is an AXI round-trip.
snooper_entry_t *cfi_window;                   // last N entries before syscall (L1)
uint32_t         cfi_window_count;             // valid entries in cfi_window[]

#if ENABLE_FETCH
static uint32_t *fetch_bytes;                  // bytes DMA'd per slot (0 = none)
#endif

static cfi_va_pa_table_t *l1_tbl;              // VA->PA table copied into L1
static uint8_t           *l1_instr;            // CFI_WINDOW_SIZE fetch slots
static uint32_t va_start_l, va_start_h;
static uint32_t va_end_l,   va_end_h;

#if ENABLE_STATS
// Per-phase cycle accumulators, summed over all inspect_window() calls (L1).
typedef struct {
    uint32_t syscall_count;
    uint32_t inspect_total_cyc;
    uint32_t last_read_cyc;       // AXI read of snooper LAST register
    uint32_t ring_read_cyc;       // ring window DMA (or N x 5 AXI loads)
    uint32_t va_to_pa_cyc;        // N cfi_va_to_pa() L1 scans
    uint32_t dma_cyc;             // iDMA issue + wait for BB fetch
} cfi_stats_t;
static cfi_stats_t *stat;
#endif

// inspect_window() context: L1 pointers + cached scalars, filled once in
// run_inspector() so the hot path never touches the L2 globals.
typedef struct {
    snooper_entry_t   *win;                // == cfi_window
    uint32_t           vs_h, vs_l, ve_l;   // monitored VA range
#if ENABLE_FETCH
    uint32_t          *fb;                 // == fetch_bytes
    uint8_t           *instr;              // == l1_instr
    cfi_va_pa_table_t *tbl;                // == l1_tbl
#endif
#if ENABLE_STATS
    cfi_stats_t       *st;                 // == stat
#endif
} inspect_ctx_t;

// Low-level accessors
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

// inspect_window — capture the last CFI_WINDOW_SIZE snooper entries and, if
// enabled, DMA-fetch their basic-block instructions into L1.
// Returns the new drain_ptr (kept in a register, never in L2).
static uint32_t inspect_window(const inspect_ctx_t *ctx, uint32_t drain_ptr) {
    snooper_entry_t *win = ctx->win;
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
    uint32_t n         = (avail < CFI_WINDOW_SIZE) ? avail : (uint32_t)CFI_WINDOW_SIZE;
    uint32_t use_bytes = n * ENTRY_SIZE;

    // Window start = (cur_last - use_bytes) mod RING_BYTES.
    uint32_t rptr;
    if (cur_last >= use_bytes)
        rptr = cur_last - use_bytes;
    else
        rptr = SNPR_RING_BYTES - (use_bytes - cur_last);

    // ---- DEBUG: snooper pointer trace (set DEBUG_PTR 0 to silence) ----------
    //   last : write pointer  (advances on every recorded branch)
    //   base : oldest pointer  (advances only after the ring has wrapped once)
    //   dl   : bytes recorded since the previous syscall (0 => nothing captured)
    //   ctrl : CTRL readback  (bit0=U_MODE, bit5=PC_RANGE_2 must stay set)
    // If last/dl never change, CVA6 stopped feeding CTR records — the ring is
    // fine, the input stream stopped.  If ctrl drops bit0/bit5, the enables got
    // auto-cleared (trigger).  If last climbs but the window is stale, the
    // seek/read is wrong.
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
    // Burst the window straight into cfi_window[].  A wrapped window needs two
    // transfers; the split always falls on an entry boundary.
    if (n > 0) {
        uint32_t wdst = (uint32_t)win;
        if (rptr + use_bytes <= SNPR_RING_BYTES) {
            plp_cl_dma_wait_toL1(
                pulp_cl_idma_L2ToL1(BASE_SNPR + rptr, wdst,
                                    (unsigned short)use_bytes));
        } else {
            uint32_t first  = SNPR_RING_BYTES - rptr;
            uint32_t second = use_bytes - first;
            // Issue both halves before blocking.
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

#if ENABLE_FETCH
    // Fetch each entry's basic block: start = previous pc_dst (or pc_src when
    // out of the monitored range), end = pc_src + 4.  VAs -> PA before the DMA.
    for (uint32_t i = 0; i < n; i++) fb[i] = 0u;

    uint32_t vs_h = ctx->vs_h, vs_l = ctx->vs_l, ve_l = ctx->ve_l;

    // Depth-3 issue: fire every BB transfer up front (each targets a distinct
    // L1 slot, so bursts never alias), then drain the whole frame in one wait.
    // The channel overlaps the bursts instead of the depth-1 issue/wait
    // ping-pong, and the PMCA receives the complete 3-BB window as one L1 frame.
    unsigned int last_id = 0;
    int have_last = 0;

    for (uint32_t i = 0; i < n; i++) {
        uint32_t bb_va_l = (i > 0) ? win[i - 1].pc_dst_l : win[i].pc_src_l;
        uint32_t bb_va_h = (i > 0) ? win[i - 1].pc_dst_h : win[i].pc_src_h;

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

    // Drain the frame: iDMA tx ids are monotonic and complete in order, so
    // the last issued id completing implies every earlier BB burst has landed.
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

// dump_last_window — print the last window (entries + decoded instructions).
#if DUMP_FETCH && ENABLE_FETCH
static void dump_last_window(void) {
    LOG("[cl-insp] === fetch dump: %u entries ===\r\n",
        (unsigned)cfi_window_count);
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
    LOG("[cl-insp] === end dump ===\r\n");
}
#endif

// arm_snooper — RANGE_2 = monitored VA range, U+M mode, halt disabled.
static void arm_snooper(void) {
    snpr_cfg_wr(CFG_REGS_CTRL_REG_OFFSET, 0u);

    // Clear the ring write pointer / full-latch before enabling so each run
    // starts recording into a fresh buffer (arm was previously missing this,
    // leaving stale entries from the prior run/fill).
    snpr_cfg_wr(CFG_REGS_CTRL_REG_OFFSET, (1u << CFG_REGS_CTRL_CNT_RST_BIT));
    snpr_cfg_wr(CFG_REGS_CTRL_REG_OFFSET, 0u);

    snpr_cfg_wr(CFG_REGS_RANGE_2_BASE_H_REG_OFFSET, va_start_h);
    snpr_cfg_wr(CFG_REGS_RANGE_2_BASE_L_REG_OFFSET, va_start_l);
    snpr_cfg_wr(CFG_REGS_RANGE_2_LAST_H_REG_OFFSET, va_end_h);
    snpr_cfg_wr(CFG_REGS_RANGE_2_LAST_L_REG_OFFSET, va_end_l);

    // The ring must wrap silently: HALT_LEVEL above the ring size so
    // core_halt_o can never assert.
    snpr_cfg_wr(CFG_REGS_HALT_LEVEL_REG_OFFSET, 0x0000FFFFu);

    // U+M mode so the inspector works with both the Linux monitor and the
    // bare-metal driver; the RANGE_2 filter keeps out-of-range PCs away.
    // Trace mode = ADDRESS (0): 20-byte PC_SRC/PC_DST/CTR_TYPE records, matching
    // ENTRY_SIZE and snooper_fetch_linux.c.  Set explicitly instead of relying
    // on the zeroed CTRL write above (use 1 = INSTRUCTION for 4-byte PC records).
    const uint32_t TRACE_MODE_ADDRESS = 0u;
    uint32_t ctrl = (1u << CFG_REGS_CTRL_U_MODE_BIT) |
                    (1u << CFG_REGS_CTRL_M_MODE_BIT) |
                    (1u << CFG_REGS_CTRL_PC_RANGE_2_BIT) |
                    ((TRACE_MODE_ADDRESS & CFG_REGS_CTRL_TRACE_MODE_MASK)
                        << CFG_REGS_CTRL_TRACE_MODE_OFFSET);
    snpr_cfg_wr(CFG_REGS_CTRL_REG_OFFSET, ctrl);

    // Readback forces the writes to complete and verifies snooper access.
    // base/last should both be 0 here (fresh CNT_RST, no branch recorded yet);
    // if last != 0 the counter reset did not take.
    uint32_t ctrl_rb = snpr_cfg_rd(CFG_REGS_CTRL_REG_OFFSET);
    LOG("[cl-insp] snooper CTRL=0x%08x (want 0x%08x) base=0x%x last=0x%x\r\n",
        (unsigned)ctrl_rb, (unsigned)ctrl,
        (unsigned)snpr_cfg_rd(CFG_REGS_BASE_REG_OFFSET),
        (unsigned)snpr_cfg_rd(CFG_REGS_LAST_REG_OFFSET));
}

static void disarm_snooper(void) {
    // Disable monitoring, reset the ring counter.
    snpr_cfg_wr(CFG_REGS_CTRL_REG_OFFSET, 0u);
    snpr_cfg_wr(CFG_REGS_CTRL_REG_OFFSET, (1u << CFG_REGS_CTRL_CNT_RST_BIT));
    snpr_cfg_wr(CFG_REGS_CTRL_REG_OFFSET, 0u);
}

// run_inspector — the full CFI inspector; executed by cluster core 0 only.
static int run_inspector(void) {
    perf_init();
    LOG("[cl-insp] cfi_syscall_cluster: start\r\n");

    // 1. Allocate L1 buffers.
    l1_tbl     = (cfi_va_pa_table_t *)pi_l1_malloc(0, sizeof(cfi_va_pa_table_t));
    l1_instr   = (uint8_t *)pi_l1_malloc(0, CFI_WINDOW_SIZE * L1_BUF_SIZE);
    cfi_window = (snooper_entry_t *)pi_l1_malloc(0, CFI_WINDOW_SIZE * sizeof(snooper_entry_t));
#if ENABLE_FETCH
    fetch_bytes = (uint32_t *)pi_l1_malloc(0, CFI_WINDOW_SIZE * sizeof(uint32_t));
#endif
#if ENABLE_STATS
    stat = (cfi_stats_t *)pi_l1_malloc(0, sizeof(cfi_stats_t));
    if (stat) *stat = (cfi_stats_t){0};   // pi_l1_malloc does not zero
#endif
    if (!l1_tbl || !l1_instr || !cfi_window
#if ENABLE_FETCH
        || !fetch_bytes
#endif
#if ENABLE_STATS
        || !stat
#endif
       ) {
        LOG("[cl-insp] ERROR: L1 alloc failed\r\n");
        hal_mailboxes_write_return_value(1);
        hal_mailboxes_ring_doorbell();
        return 1;
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
        hal_mailboxes_write_return_value(1);
        hal_mailboxes_ring_doorbell();
        return 1;
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

    // 5. Steady-state loop — one iteration per Ibex mailbox kick.
    // iDMA clock stays on for the whole loop (per-call gating was overhead).
    plp_idma_enable_clk();

    inspect_ctx_t ctx;
    ctx.win  = cfi_window;
    ctx.vs_h = va_start_h;
    ctx.vs_l = va_start_l;
    ctx.ve_l = va_end_l;
#if ENABLE_FETCH
    ctx.fb    = fetch_bytes;
    ctx.instr = l1_instr;
    ctx.tbl   = l1_tbl;
#endif
#if ENABLE_STATS
    ctx.st = stat;
#endif

    int app_done = 0;
    uint32_t syscalls = 0;
    uint32_t drain_ptr = 0;   // software ring read pointer
    while (!app_done) {
        while (hal_mailboxes_receive_irq_pending() == 0)
            ;
        uint32_t msg = (uint32_t)hal_mailboxes_read_letter0();
        hal_mailboxes_clear_receive_irq();

        uint32_t msg_type = msg & CFI_MSG_TYPE_MASK;

        if (msg_type == CFI_MSG_APP_DONE) {
            disarm_snooper();
            app_done = 1;
            LOG("[cl-insp] app done\r\n");
        } else if (msg_type == CFI_MSG_SYSCALL) {
            uint32_t nr = msg & CFI_MSG_SYSCALL_NR_MASK;
            (void)nr;   // available for future per-syscall policy
            drain_ptr = inspect_window(&ctx, drain_ptr);
            // PMCA hook: swap this PASS write for a PMCA/NN offload call.
            host_scratch_wr(CFI_SCRATCH_RESULT_OFF, CFI_RESULT_PASS);
            syscalls++;
        } else {
            // Unknown message: never stall CVA6.
            host_scratch_wr(CFI_SCRATCH_RESULT_OFF, CFI_RESULT_PASS);
            LOG("[cl-insp] unknown msg 0x%08x\r\n", (unsigned)msg);
        }
    }

    plp_idma_disable_clk();

    // 6. Aggregate statistics.
#if ENABLE_STATS
    {
        uint32_t nn        = stat->syscall_count ? stat->syscall_count : 1u;
        uint32_t avg_total = stat->inspect_total_cyc / nn;
#if STATS_PER_PHASE
        uint32_t avg_last  = stat->last_read_cyc     / nn;
        uint32_t avg_ring  = stat->ring_read_cyc     / nn;
        uint32_t avg_vatopa= stat->va_to_pa_cyc      / nn;
        uint32_t avg_dma   = stat->dma_cyc           / nn;

        // Overhead = total minus measured phases.
        uint32_t sum_phases = avg_last + avg_ring + avg_vatopa + avg_dma;
        uint32_t avg_ovhd   = (avg_total > sum_phases) ? avg_total - sum_phases : 0u;

        // Calibrate one rdcycle() read with two back-to-back reads.
        uint32_t _c0 = rdcycle();
        uint32_t _c1 = rdcycle();
        uint32_t rd_cal = _c1 - _c0;

        // rdcycle reads landing in the overhead gap: 4 fixed + 2 per entry
        // (worst-case estimate).
        uint32_t n_rd_gap = 4u + 2u * (uint32_t)CFI_WINDOW_SIZE;
        uint32_t rd_cost  = n_rd_gap * rd_cal;
        uint32_t logic_ov = (avg_ovhd > rd_cost) ? avg_ovhd - rd_cost : 0u;

        LOG("[cl-insp] stats over %u syscalls (window=%u, rdcycle=%u cyc):\r\n",
            (unsigned)stat->syscall_count, (unsigned)CFI_WINDOW_SIZE, (unsigned)rd_cal);
        LOG("[cl-insp]   last_read     avg=%5u cyc  (AXI read of snooper LAST)\r\n",
            (unsigned)avg_last);
        LOG("[cl-insp]   ring_read     avg=%5u cyc\r\n", (unsigned)avg_ring);
        LOG("[cl-insp]   va_to_pa      avg=%5u cyc\r\n", (unsigned)avg_vatopa);
        LOG("[cl-insp]   dma_fetch     avg=%5u cyc\r\n", (unsigned)avg_dma);
        LOG("[cl-insp]   overhead      avg=%5u cyc  (total - measured phases)\r\n",
            (unsigned)avg_ovhd);
        LOG("[cl-insp]   rdcycle_cost  avg=%5u cyc  (%u reads x %u cyc)\r\n",
            (unsigned)rd_cost, (unsigned)n_rd_gap, (unsigned)rd_cal);
        LOG("[cl-insp]   logic_ovhd    avg=%5u cyc  (overhead - rdcycle_cost)\r\n",
            (unsigned)logic_ov);
#else
        LOG("[cl-insp] stats over %u syscalls (window=%u, whole-window timing, "
            "2 rdcycle reads/call):\r\n",
            (unsigned)stat->syscall_count, (unsigned)CFI_WINDOW_SIZE);
#endif
        LOG("[cl-insp]   inspect_total avg=%5u cyc\r\n", (unsigned)avg_total);
    }
#endif

#if DUMP_FETCH && ENABLE_FETCH
    dump_last_window();
#endif

    LOG("[cl-insp] done: syscalls=%u\r\n", (unsigned)syscalls);

    // 7. Release the Ibex firmware (SECD SND doorbell + return value).
    hal_mailboxes_write_return_value(0);
    hal_mailboxes_ring_doorbell();
    return 0;
}

// main — core 0 runs the inspector; all cores rendezvous before returning.
int main(void) {
    uint32_t core_id = (uint32_t)rt_core_id();
    int ret = 0;

    if (core_id == 0)
        ret = run_inspector();

    synch_barrier();
    return ret;
}
