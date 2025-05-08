#include "idma_3d_transfer.h"

#define MAX_BUFFER_SIZE 0x2200
L1_DATA src[MAX_BUFFER_SIZE];
L2_DATA dst[MAX_BUFFER_SIZE];

int idma_3d_transfer (int k, int ext2loc) {
    uint32_t dma_src_start_addr;
    uint32_t dma_dst_start_addr;
    uint32_t nb_words = transfer_params[k].nb_words;
    uint32_t transfer_size;
    int errors = 0;

    
    // Extract the source and destination strides from the stimuli file
    uint32_t src_addr_stride_2d = transfer_params[k].src_stride_2d * sizeof(uint32_t); // Stride to compute the source address in the iDMA midend
    uint32_t dst_addr_stride_2d = transfer_params[k].dst_stride_2d * sizeof(uint32_t); // Stride to compute the destination address in the iDMA midend
    
    uint32_t src_addr_stride_3d = transfer_params[k].src_stride_3d * sizeof(uint32_t); // Stride to compute the source address in the iDMA midend
    uint32_t dst_addr_stride_3d = transfer_params[k].dst_stride_3d * sizeof(uint32_t); // Stride to compute the destination address in the iDMA midend
    
    uint32_t reps_3d = transfer_params[k].reps_3d; // Number of 2D pages to be transferred
    uint32_t reps_2d = nb_words;
    
    // Put the starting addresses of the source and destination spaces in these variables
    dma_src_start_addr = (int)&src;
    dma_dst_start_addr = (int)&dst;
    
    uint32_t *src_addr;
    uint32_t *dst_addr;
    
    PRINTF ("Transfer %d \n", k);
    PRINTF ("Number of words to be transferred: %d \n", nb_words);
    PRINTF ("Source address: %8x \n", dma_src_start_addr);
    PRINTF ("Destination address: %8x \n", dma_dst_start_addr);
    PRINTF ("Source address stride: %8x \n", src_addr_stride_2d);
    PRINTF ("Destination address stride: %8x \n", dst_addr_stride_2d);
    PRINTF ("Source address stride 3D: %8x \n", src_addr_stride_3d);
    PRINTF ("Destination address stride 3D: %8x \n", dst_addr_stride_3d);
    PRINTF ("Number of 2D pages to be transferred: %d \n", reps_3d);

    // Fill src array with test data
    src_addr = dma_src_start_addr;
    for (int j=0; j<reps_3d; j++) {
        for (int i = 0; i < nb_words; i++) {
            *src_addr = i+1+j*nb_words;
            if (i<nb_words-1) {
                src_addr += transfer_params[k].src_stride_2d;
            }
        }
        src_addr += transfer_params[k].src_stride_3d;
    }

    // Clear the destination array
    dst_addr = dma_dst_start_addr;
    for (int j=0; j<reps_3d; j++) {
        for (int i = 0; i < nb_words; i++) {
            *dst_addr = 0;
            if (i<nb_words-1) {
                dst_addr += transfer_params[k].dst_stride_2d;
            }
        }
        dst_addr += transfer_params[k].dst_stride_3d;
    }

    // The transfer size is the number of words to be transferred multiplied by the size of the data type (4 bytes for uint32_t)
    // multiplied by the source stride
    transfer_size = nb_words * sizeof(uint32_t) * transfer_params[k].reps_3d;

    // Execute the iDMA transfer and wait for its completion
    plp_dma_wait(plp_dma_memcpy_3d(dma_dst_start_addr, dma_src_start_addr, transfer_size, dst_addr_stride_2d, src_addr_stride_2d, dst_addr_stride_3d, src_addr_stride_3d, reps_2d, reps_3d, ext2loc));

    // Loop on the transfer size to check if the transfer was successful
    src_addr = dma_src_start_addr;
    dst_addr = dma_dst_start_addr;
    for (int j=0; j<reps_3d; j++) {
        for (int i = 0; i < nb_words; i++) {
            if (*dst_addr != *src_addr) {
                PRINTF("ERRORS ==> @%8x Dst[%d]: %d vs @%8x Src[%d]: %d \n", dst_addr, i+j*nb_words, *dst_addr, src_addr, i+j*nb_words, *src_addr);
                errors++;
            }
            if (i < nb_words-1) {
                src_addr += transfer_params[k].src_stride_2d;
                dst_addr += transfer_params[k].dst_stride_2d;
            }
        }
        src_addr += transfer_params[k].src_stride_3d;
        dst_addr += transfer_params[k].dst_stride_3d;
    }

    // Clear both the source and the destination arrays to avoid issues with the following transfer
    
    src_addr = dma_src_start_addr;
    dst_addr = dma_dst_start_addr;

    for (int j=0; j<reps_3d; j++) {
        for (int i = 0; i < nb_words; i++) {
            *src_addr = 0;
            if (i<nb_words-1) {
                src_addr += transfer_params[k].src_stride_2d;
            }
        }
        src_addr += transfer_params[k].src_stride_3d;
    }

    for (int j=0; j<reps_3d; j++) {
        for (int i = 0; i < nb_words; i++) {
            *dst_addr = 0;
            if (i<nb_words-1) {
                dst_addr += transfer_params[k].dst_stride_2d;
            }
        }
        dst_addr += transfer_params[k].dst_stride_3d;
    }

    return errors;
}

int main() {
    uint32_t errors[NB_TRANSFERS] = {0};
    int test_status = 0;

    if (TEST_ALL_CORES) {
        for (int core=0; core<ARCHI_CLUSTER_NB_PE; core++) {
            if (get_core_id() == core) {
                PRINTF("Testing iDMA with cluster core %d\n", core);

                for (int k = 0; k < NB_TRANSFERS; k++) {
                    /* Local memory to external */
                    errors[k] += idma_3d_transfer(k, 0);
                    PRINTF("Transfer  L1 -> L2 %d finished with %d errors \n", k, errors[k]);
                    PRINTF("--------------------------------------------------\n");
                    /* External memory to local */
                    errors[k] += idma_3d_transfer(k, 1);
                    PRINTF("Transfer L2 -> L1 %d finished with %d errors \n", k, errors[k]);
                    PRINTF("--------------------------------------------------\n");
                    if (errors[k] != 0) {
                        test_status = 1;
                    }
                }
            }
            synch_barrier();
        }
    } else {
        // Executes only on Core 0
        if (rt_core_id() == 0) {
        // Loop on the number of simple transfers to be executed by the iDMA (specified in idma_defines.h)
        for (int k = 0; k < NB_TRANSFERS; k++) {
            /* Local memory to external */
            errors[k] += idma_3d_transfer(k, 0);
            PRINTF("Transfer  L1 -> L2 %d finished with %d errors \n", k, errors[k]);
            PRINTF("--------------------------------------------------\n");
            /* External memory to local */
            errors[k] += idma_3d_transfer(k, 1);
            PRINTF("Transfer L2 -> L1 %d finished with %d errors \n", k, errors[k]);
            PRINTF("--------------------------------------------------\n");
            if (errors[k] != 0) {
                test_status = 1;
            }
        }
    }
    }
    return test_status;
}
