#include "idma_multi_core.h"

#define MAX_BUFFER_SIZE 0x2200

L1_DATA src[MAX_BUFFER_SIZE];
L2_DATA dst[MAX_BUFFER_SIZE];

static int errors_global = 0;

int main () {

    uint32_t errors = 0;
    uint32_t dma_src_start_addr;
    uint32_t dma_dst_start_addr;
    uint32_t *src_addr;
    uint32_t *dst_addr;
    uint32_t addr_offset = 0;

    int core_id = rt_core_id();

    for (int k=0; k<NB_TRANSFERS; k++) {
        // Synchronize all cores before allocating the buffers
        addr_offset = nb_words[k] * core_id * sizeof(uint32_t);

        dma_src_start_addr = (uint32_t)&src + addr_offset;
        dma_dst_start_addr = (uint32_t)&dst + addr_offset;

        // Fill the source array with test data
        for (int i=0; i<nb_words[k]; i++) {
            src_addr = (uint32_t *)(dma_src_start_addr + i * sizeof(uint32_t));
            *src_addr = i+1;
        }
    
        // Clear the destination array
        for (int i=0; i<nb_words[k]; i++) {
            dst_addr = (uint32_t *)(dma_dst_start_addr + i * sizeof(uint32_t));
            *dst_addr = nb_words[k]-i;
        }

        // A bit of randomness on this parameter to have each core perform transfers in both directions
        int ext2loc = (core_id+nb_words[k]) % 2;

        plp_cl_dma_wait(plp_cl_dma_memcpy(dma_dst_start_addr, dma_src_start_addr, nb_words[k] * sizeof(uint32_t), ext2loc));

        // Loop on the number of words moved by the iDMA for the current transfer
        for (int i = 0; i < nb_words[k]; i++) {
            src_addr = (uint32_t *)(dma_src_start_addr + i * sizeof(uint32_t));
            dst_addr = (uint32_t *)(dma_dst_start_addr + i * sizeof(uint32_t));

            if (*dst_addr != *src_addr) {
                errors++; 
            }
        }

        // Clear the source array with test data
        for (int i=0; i<nb_words[k]; i++) {
            src_addr = (uint32_t *)(dma_src_start_addr + i * sizeof(uint32_t));
            *src_addr = 0;
        }
    
        // Clear the destination array
        for (int i=0; i<nb_words[k]; i++) {
            dst_addr = (uint32_t *)(dma_dst_start_addr + i * sizeof(uint32_t));
            *dst_addr = 0;
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

        // Synchronize all cores before updating the memory boundaries for each core -->
        // Cores that are already setting up the next transfer might overlap with cores
        // that are still checking results from the previous transfer.

        synch_barrier();
    }

    // Synchronize all cores before exiting the test
    synch_barrier();

    return errors_global;
}
