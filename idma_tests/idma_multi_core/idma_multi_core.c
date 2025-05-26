#include "idma_multi_core.h"

#define MAX_BUFFER_SIZE 0x2200
#define CORE_SPACE 0x1000

L2_DATA ext[MAX_BUFFER_SIZE];
L1_DATA loc[MAX_BUFFER_SIZE];

int errors[8] = {0};
int test_status = 0;

int test_idma_1D (int core_id, uint32_t size, int ext2loc, uint32_t ext_addr, uint32_t tcdm_addr) {
    volatile uint8_t *src_ptr, *dst_ptr;

    int error = 0;

    if (ext2loc == 1) {
        // L2 to L1 transfer
        src_ptr = (uint8_t*) ext_addr;
        dst_ptr = (uint8_t*) tcdm_addr;

        // Fill source region with test data
        for (int i = 0; i < size; i++) {
                src_ptr[i] = (uint8_t)(i & 0xFF);
        }

        pulp_cl_idma_L2ToL1((unsigned int) src_ptr, (unsigned int) dst_ptr, size);

    } else {
        // L1 to L2 transfer
        src_ptr = (uint8_t*) tcdm_addr;
        dst_ptr = (uint8_t*) ext_addr;

        // Fill source region with test data
        for (int i = 0; i < size; i++) {
            src_ptr[i] = (uint8_t)(i & 0xFF);
        }

        pulp_cl_idma_L1ToL2((unsigned int) src_ptr, (unsigned int) dst_ptr, size);
    }

    plp_cl_dma_barrier();

    // Check the results

    for (int i=0; i < size; i++) {
        uint8_t expected = src_ptr[i]; 
        uint8_t actual   = dst_ptr[i];

        if (expected != actual) {
            error++;
            if (core_id == 0) {
                PRINTF ("Error: expected @%8x = %8x vs actual @%8x = %8x \n", expected, &src_ptr[i], actual, &dst_ptr[i]);
            }
        }
    }

    return error;
}

int main () {
    int core_id = rt_core_id();

    unsigned int size;
    uint32_t ext_addr;
    uint32_t loc_addr;

    ext_addr = (uint32_t)ext + core_id * CORE_SPACE;
    loc_addr = (uint32_t)loc + core_id * CORE_SPACE;

    #ifdef MULTI_CORE_P
        // MULTI CORE PARALLEL MODE: each core uses the iDMA in a parallel manner
        if (core_id == 0) {
            PRINTF ("MULTI CORE PARALLEL MODE \n");
        }
        for (int k = 0; k < NB_TRANSFERS; k++) {
            size = sizes[k];
            if (core_id == 0) {
                PRINTF ("Transfer: %d | Size: %d \n", k, size);
            }
            errors[core_id] += test_idma_1D(core_id, size, (core_id % 2), ext_addr, loc_addr);
            synch_barrier();
        }
    #elif MULTI_CORE_S
        // MULTI CORE SERIAL MODE: each core uses the iDMA in a serial manner
        if (core_id == 0) {
            PRINTF ("MULTI CORE SERIAL MODE \n");
        }
        for (int i = 0; i < 8; i++) {
            if (core_id == i) {
                for (int k = 0; k < NB_TRANSFERS; k++) {
                    size = sizes[k];
                    if (core_id == 0) {
                        PRINTF ("Transfer: %d | Size: %d \n", k, size);
                    }
                    errors[core_id] += test_idma_1D(core_id, size, (core_id% 2), ext_addr, loc_addr);
                }
            }
        }
    #else
        if (core_id == 0) {
            // SINGLE CORE MODE: just core 0 uses the iDMA
            PRINTF ("Just using Core 0 \n");
            for (int k = 0; k < NB_TRANSFERS; k++) {
                size = sizes[k];
                PRINTF ("Transfer: %d | Size: %d \n", k, size);
                errors[core_id] += test_idma_1D(core_id, size, 0, ext_addr, loc_addr);
                errors[core_id] += test_idma_1D(core_id, size, 1, ext_addr, loc_addr);
            }
        }
    #endif

    if (core_id == 0) {
        for (int i = 0; i<8; i++) {
            if (errors[i] !=0) {
                PRINTF ("Core %d returned %d errors \n", i, errors[i]);
                test_status = 1;
            }
        }
    }

    return test_status;
}
