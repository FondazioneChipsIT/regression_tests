#include "idma_multi_core.h"

#define MAX_BUFFER_SIZE 0x2200
#define CORE_SIZE 0x20
L1_DATA src[MAX_BUFFER_SIZE];
L2_DATA dst[MAX_BUFFER_SIZE];

unsigned int num_cores;

static int errors_global = 0;

int main () {

    uint32_t errors = 0;
    uint32_t dma_src_start_addr;
    uint32_t dma_dst_start_addr;
    uint32_t *src_addr;
    uint32_t *dst_addr;
    uint32_t addr_offset = 0;
    
    uint32_t core_id;

    if (rt_cluster_id() != 0) {
        return bench_cluster_forward(0);
    }

    num_cores = get_core_num();

    core_id = rt_core_id();
    addr_offset = CORE_SIZE * core_id * sizeof(uint32_t);

    dma_src_start_addr = (uint32_t)&src + addr_offset;
    dma_dst_start_addr = (uint32_t)&dst + addr_offset;
    
    // Fill the source array with test data
    for (int i=0; i<CORE_SIZE; i++) {
        src_addr = (uint32_t *)((uint8_t *)dma_src_start_addr + i * sizeof(uint32_t));
        *src_addr = i+1;
    }
            
    // Clear the destination array
    for (int i=0; i<CORE_SIZE; i++) {
        dst_addr = (uint32_t *)((uint8_t *)dma_dst_start_addr + i * sizeof(uint32_t));
        *dst_addr = 0x20-i;
    }

    plp_cl_dma_memcpy(dma_dst_start_addr, dma_src_start_addr, CORE_SIZE * sizeof(uint32_t), core_id%2);

    // Wait for all iDMA transfers to finish
    plp_cl_dma_barrier();

    // Synchronize all cores
    synch_barrier();

    // Loop on the number of words moved by the iDMA for the current transfer
    for (int i = 0; i < CORE_SIZE; i++) {
        src_addr = (uint32_t *)((uint8_t *)dma_src_start_addr + i * sizeof(uint32_t));
        dst_addr = (uint32_t *)((uint8_t *)dma_dst_start_addr + i * sizeof(uint32_t));

        if (*dst_addr != *src_addr) {
            if (core_id == 0) {
                PRINTF("Error in core %d: @%8x src[%d] = %d, @%8x dst[%d] = %d\n", core_id, src_addr, i, *src_addr, dst_addr, i, *dst_addr);
            }
            if (core_id == 1) {
                PRINTF("Error in core %d: @%8x src[%d] = %d, @%8x dst[%d] = %d\n", core_id, src_addr, i, *src_addr, dst_addr, i, *dst_addr);
            }
            if (core_id == 2) {
                PRINTF("Error in core %d: @%8x src[%d] = %d, @%8x dst[%d] = %d\n", core_id, src_addr, i, *src_addr, dst_addr, i, *dst_addr);
            }
            if (core_id == 3) {
                PRINTF("Error in core %d: @%8x src[%d] = %d, @%8x dst[%d] = %d\n", core_id, src_addr, i, *src_addr, dst_addr, i, *dst_addr);
            }
            if (core_id == 4) {
                PRINTF("Error in core %d: @%8x src[%d] = %d, @%8x dst[%d] = %d\n", core_id, src_addr, i, *src_addr, dst_addr, i, *dst_addr);
            }
            if (core_id == 5) {
                PRINTF("Error in core %d: @%8x src[%d] = %d, @%8x dst[%d] = %d\n", core_id, src_addr, i, *src_addr, dst_addr, i, *dst_addr);
            }
            if (core_id == 6) {
                PRINTF("Error in core %d: @%8x src[%d] = %d, @%8x dst[%d] = %d\n", core_id, src_addr, i, *src_addr, dst_addr, i, *dst_addr);
            }
            if (core_id == 7) {
                PRINTF("Error in core %d: @%8x src[%d] = %d, @%8x dst[%d] = %d\n", core_id, src_addr, i, *src_addr, dst_addr, i, *dst_addr);
            }
            errors++;
        }
    }


    if (core_id == 0) {
        errors_global += errors;
    }
    if (core_id == 1) {
        errors_global += errors;
    }
    if (core_id == 2) {
        errors_global += errors;
    }
    if (core_id == 3) {
        errors_global += errors;
    }
    if (core_id == 4) {
        errors_global += errors;
    }
    if (core_id == 5) {
        errors_global += errors;
    }
    if (core_id == 6) {
        errors_global += errors;
    }
    if (core_id == 7) {
        errors_global += errors;
    }
    
    synch_barrier();

    return errors_global;
}
