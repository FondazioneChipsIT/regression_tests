#include "idma_2d_transfer.h"

#define MAX_BUFFER_SIZE 0x2200
L1_DATA src[MAX_BUFFER_SIZE];
L2_DATA dst[MAX_BUFFER_SIZE];

int idma_2d_transfer (int k, int ext2loc) {
    uint32_t dma_src_start_addr;
    uint32_t dma_dst_start_addr;
    uint32_t nb_words = transfer_params[k].nb_words;
    uint32_t transfer_size;
    int errors = 0;

    PRINTF ("Transfer %d \n", k);
    PRINTF ("Number of words to be transferred: %d \n", nb_words);
    PRINTF ("src_stride: %d \n", transfer_params[k].src_stride);
    PRINTF ("dst_stride: %d \n", transfer_params[k].dst_stride);

    // Extract the source and destination strides from the stimuli file
    uint32_t src_addr_stride = transfer_params[k].src_stride * sizeof(uint32_t); // Stride to compute the source address in the iDMA midend
    uint32_t dst_addr_stride = transfer_params[k].dst_stride * sizeof(uint32_t); // Stride to compute the destination address in the iDMA midend

    // Put the starting addresses of the source and destination spaces in these variables
    dma_src_start_addr = (int)&src;
    dma_dst_start_addr = (int)&dst;

    uint32_t *src_addr;
    uint32_t *dst_addr;

    // Fill src array with test data
    for (int i = 0; i < nb_words; i++) {
        src_addr = (uint32_t *)((uint8_t *)dma_src_start_addr + i * src_addr_stride);
        *src_addr = i+1;
    }

    // Clear the destination array
    for (int i = 0; i < nb_words; i++) {
        dst_addr = (uint32_t *)((uint8_t *)dma_dst_start_addr + i * dst_addr_stride);
        *dst_addr = 0;
    }

    // The transfer size is the number of words to be transferred multiplied by the size of the data type (4 bytes for uint32_t)
    // multiplied by the source stride
    transfer_size = nb_words * sizeof(uint32_t) * transfer_params[k].src_stride;

    // Execute the iDMA transfer and wait for its completion
    plp_dma_wait(plp_dma_memcpy_2d(dma_dst_start_addr, dma_src_start_addr, transfer_size, dst_addr_stride, src_addr_stride, 0));

    // Loop on the transfer size to check if the transfer was successful
    for (int i = 0; i < nb_words; i++) {
        src_addr = (uint32_t *)((uint8_t *)dma_src_start_addr+ i * src_addr_stride);
        dst_addr = (uint32_t *)((uint8_t *)dma_dst_start_addr+ i * dst_addr_stride);
        if (*dst_addr != *src_addr) {
            PRINTF("ERROR ==> Dst[%d]: %d vs Src[%d]: %d \n", i, *dst_addr, i, *src_addr);
            errors++;
        }
    }

    // Clear both the source and the destination arrays to avoid issues with the following transfer
    for (int i = 0; i < nb_words; i++) {
        src_addr = (uint32_t *)((uint8_t *)dma_src_start_addr+ i * src_addr_stride);
        *src_addr = 0;
    }

    for (int i = 0; i < nb_words; i++) {
        dst_addr = (uint32_t *)((uint8_t *)dma_dst_start_addr + i * dst_addr_stride);
        *dst_addr = 0;
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
                    errors[k] += idma_2d_transfer(k, 0);
                    /* External memory to local */
                    errors[k] += idma_2d_transfer(k, 1);
                    if (errors[k] != 0) {
                        test_status = 1;
                    }
                    PRINTF("Transfer %d finished with %d errors \n", k, errors);
                    PRINTF("--------------------------------------------------\n");
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
            errors[k] += idma_2d_transfer(k, 0);
            /* External memory to local */
            errors[k] += idma_2d_transfer(k, 1);
            if (errors[k] != 0) {
                test_status = 1;
            }
            PRINTF("Transfer %d finished with %d errors \n", k, errors);
            PRINTF("--------------------------------------------------\n");
        }
    }
    }
    return test_status;
}
