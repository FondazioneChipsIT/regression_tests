#include "idma_2d_transfer.h"

int main() {
    uint32_t errors[NB_TRANSFERS] = {0};
    int test_status = 0;

    // Executes only on Core 0
    if (rt_core_id() == 0) {
        // Loop on the number of simple transfers to be executed by the iDMA (specified in idma_defines.h)
        for (int k = 0; k < NB_TRANSFERS; k++) {
            uint32_t dma_src_start_addr;
            uint32_t dma_dst_start_addr;
            uint32_t nb_words = transfer_params[k].nb_words;
            uint32_t transfer_size;

            PRINTF ("Transfer %d \n", k);
            PRINTF ("Number of words to be transferred: %d \n", nb_words);
            PRINTF ("src_stride: %d \n", transfer_params[k].src_stride);
            PRINTF ("dst_stride: %d \n", transfer_params[k].dst_stride);

            // Extract the source and destination strides from the stimuli file
            uint32_t src_addr_stride = transfer_params[k].src_stride * sizeof(uint32_t); // Stride to compute the source address in the iDMA midend
            uint32_t dst_addr_stride = transfer_params[k].dst_stride * sizeof(uint32_t); // Stride to compute the destination address in the iDMA midend
            
            // Extract number of words to be transferred for k-th transfer
            int src_size = nb_words;
            int dst_size = nb_words;

            // Check if the source and destination sizes are equal
            if (src_size != dst_size) {
                PRINTF("CONFIGURATION ERROR ==> src_size != dst_size \n");
                test_status = 1;
                return test_status;
            }

            // Allocate space for the source and destination arrays
            uint32_t src[nb_words*src_addr_stride];
            uint32_t dst[nb_words*dst_addr_stride];

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
                PRINTF ("@%8x -> dst[%d]: %d vs @%8x -> src[%d]: %d  \n", dst_addr, i, *dst_addr, src_addr, i, *src_addr);
                if (*dst_addr != *src_addr) {
                    test_status = 1;
                    PRINTF("ERROR ==> Dst[%d]: %d vs Src[%d]: %d \n", i, *dst_addr, i, *src_addr);
                    errors[k] = errors[k] + 1;
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
            PRINTF("Transfer %d finished with %d errors \n", k, errors[k]);
            PRINTF("--------------------------------------------------\n");
        }
        printf ("TEST SUMMARY: \n");
        for (int k = 0; k < NB_TRANSFERS; k++) {
            if (errors[k] != 0) {
                printf("Transfer %d KO with %d errors \n", k, errors[k]);
            } else {
                printf("Transfer %d OK \n", k);
            }
            printf ("--------------------------------------------------\n");
        }
        return test_status;
    }
}
