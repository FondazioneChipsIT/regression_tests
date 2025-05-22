#include "idma_multi_core_3d.h"

#define MAX_BUFFER_SIZE 0x2000
#define CORE_SPACE 0x1000

L2_DATA static uint8_t ext[MAX_BUFFER_SIZE];
L1_DATA static uint8_t loc[MAX_BUFFER_SIZE];

int errors[8] = {0};
int test_status = 0;

int test_idma_3D (int core_id, uint32_t size, int ext2loc, uint32_t ext_addr, uint32_t tcdm_addr, unsigned int length, unsigned int src_stride_2d, unsigned int dst_stride_2d, unsigned int num_reps, unsigned int src_stride_3d, unsigned int dst_stride_3d, unsigned int num_reps_3d) {
    volatile uint8_t *src_ptr, *dst_ptr;
    unsigned int max_stride, offset_3d;

    if (src_stride_2d > dst_stride_2d) {
        max_stride = src_stride_2d;
    } else {
        max_stride = dst_stride_2d;
    }

    int error = 0;

    if (ext2loc == 1) {

        // L2 to L1 transfer
        src_ptr = (uint8_t*) ext_addr;
        dst_ptr = (uint8_t*) tcdm_addr;

        // Fill source region with test data
        offset_3d = 0;
        for (int j = 0; j < num_reps_3d; j++) {
            for (int i = 0; i < size * max_stride; i++) {
                src_ptr[i+offset_3d] = (uint8_t)(i & 0xFF);
                if (core_id == 0) {
                    PRINTF ("Src_ptr[%d] @%8x = %8x \n", i+offset_3d, &src_ptr[i+offset_3d], src_ptr[i+offset_3d]);
                }
            }
            offset_3d += size * max_stride;
        }

        pulp_cl_idma_L2ToL1_3d((unsigned int)src_ptr, (unsigned int)dst_ptr, length, src_stride_2d, dst_stride_2d, num_reps, src_stride_3d, dst_stride_3d, num_reps_3d);
    } else {

        // L1 to L2 transfer
        src_ptr = (uint8_t*) tcdm_addr;
        dst_ptr = (uint8_t*) ext_addr;

        // Fill source region with test data
        offset_3d = 0;
        for (int j = 0; j < num_reps_3d; j++) {
            for (int i = 0; i < size * max_stride; i++) {
                src_ptr[i+offset_3d] = (uint8_t)(i & 0xFF);
            }
            offset_3d += size * max_stride;
        }

        pulp_cl_idma_L1ToL2_3d((unsigned int)src_ptr, (unsigned int)dst_ptr, length, src_stride_2d, dst_stride_2d, num_reps, src_stride_3d, dst_stride_3d, num_reps_3d);
    }

    plp_cl_dma_barrier();

    // Check the results
    unsigned int src_offset_2d = 0;
    unsigned int dst_offset_2d = 0;
    unsigned int src_offset_3d = 0;
    unsigned int dst_offset_3d = 0;

    for (int rep_3d = 0; rep_3d < num_reps_3d; rep_3d ++) {
        for (unsigned int rep = 0; rep < num_reps; rep++) {
            src_offset_2d = rep * src_stride_2d;
            dst_offset_2d = rep * dst_stride_2d;
            for (unsigned int i = 0; i < length; i++) {
                uint8_t expected = src_ptr[src_offset_2d + i + src_offset_3d];
                uint8_t actual   = dst_ptr[dst_offset_2d + i + dst_offset_3d];
                if (core_id == 0) {
                    
                }
                if (expected != actual) {
                    error++;
                    PRINTF ("ERROR: expected @%8x[%d] = %8x vs actual @%8x[%d] = %8x \n", &src_ptr[src_offset_2d + i + src_offset_3d], src_offset_2d + i + src_offset_3d, expected, &dst_ptr[dst_offset_2d + i + dst_offset_3d], dst_offset_2d + i + dst_offset_3d, actual);
                }
                    
            }
        }
        src_offset_3d = src_offset_2d + src_stride_3d;
        dst_offset_3d = dst_offset_2d + dst_stride_3d;
    }

    // Clear the source and destination regions

    if (ext2loc == 1) {

        // L2 to L1 transfer
        src_ptr = (uint8_t*) ext_addr;
        dst_ptr = (uint8_t*) tcdm_addr;

        // Clear the source region
        for (int i = 0; i < size * max_stride; i++) {
                src_ptr[i] = (uint8_t)(0 & 0xFF);
        }
        // Clear the destination region
        for (int i = 0; i < size * max_stride; i++) {
                dst_ptr[i] = (uint8_t)(0 & 0xFF);
        }

        
    } else {

        // L1 to L2 transfer
        src_ptr = (uint8_t*) tcdm_addr;
        dst_ptr = (uint8_t*) ext_addr;

        // Clear the source region
        for (int i = 0; i < size * max_stride; i++) {
                src_ptr[i] = (uint8_t)(0 & 0xFF);
        }
        // Clear the destination region
        for (int i = 0; i < size * max_stride; i++) {
                dst_ptr[i] = (uint8_t)(0 & 0xFF);
        }
    }

    
    return error;
}

int main () {

    int core_id = rt_core_id();

    unsigned int size, length, src_stride_2d, dst_stride_2d;
    unsigned int num_reps_3d, src_stride_3d, dst_stride_3d;
    uint32_t ext_addr;
    uint32_t loc_addr;

    ext_addr = (uint32_t)ext + core_id * CORE_SPACE;
    loc_addr = (uint32_t)loc + core_id * CORE_SPACE;

    #ifdef TEST_ALL_CORES
        // MULTI CORE MODE: all cores in parallel use the iDMA
        for (int k = 0; k < NB_TRANSFERS; k++) {
            size = transfer_params[k].size;
            length = transfer_params[k].length;
            // num_reps = transfer_params[k].num_reps;
            src_stride_2d = transfer_params[k].src_stride_2d;
            dst_stride_2d = transfer_params[k].dst_stride_2d;

            // errors[core_id] += test_idma_2D(core_id, size, (core_id%2), ext_addr, loc_addr, length, src_stride_2d, dst_stride_2d, size/length);
            // synch_barrier();
        }
    #else
        // SINGLE CORE MODE: just core 0 uses the iDMA
        if (core_id == 0) {
            for (int k = 0; k < NB_TRANSFERS; k++) {
                size = transfer_params[k].size;
                length = transfer_params[k].length;
                // num_reps = transfer_params[k].num_reps;
                src_stride_2d = transfer_params[k].src_stride_2d;
                dst_stride_2d = transfer_params[k].dst_stride_2d;
                src_stride_3d = transfer_params[k].src_stride_3d;
                dst_stride_3d = transfer_params[k].dst_stride_3d;
                num_reps_3d   = transfer_params[k].num_reps_3d;
                PRINTF ("Transfer: %d \n", k);
                PRINTF ("Size: %d | Length: %d | Src_stride_2d: %d | Dst_stride_2d: %d | Num_reps_2d: %d \n", size, length, src_stride_2d, dst_stride_2d, (size/length));
                PRINTF ("Src_stride_3d: %d | Dst_stride_3d: %d | Num_reps_3d: %d \n", src_stride_3d, dst_stride_3d, num_reps_3d);
                errors[core_id] += test_idma_3D(core_id, size, (core_id%2), ext_addr, loc_addr, length, src_stride_2d, dst_stride_2d, (size/length), src_stride_3d, dst_stride_3d, num_reps_3d);
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