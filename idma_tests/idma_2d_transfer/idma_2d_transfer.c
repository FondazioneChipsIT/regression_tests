#include "idma_2d_transfer.h"

int main() {
    uint32_t errors[NB_TRANSFERS] = {0};
    int test_status = 0;

    // Executes only on Core 0
    if (rt_core_id() == 0) {
        // Loop on the number of simple transfers to be executed by the iDMA (specified in idma_defines.h)
        for (int k = 0; k < NB_TRANSFERS; k++) {
            uint32_t dma_src;
            uint32_t dma_dst;

            uint32_t len_2d     = 4; // Effectively passed as stride at the destination memory
            uint32_t stride_2d  = 16; // Effectively passed as stride at the source memory

            uint32_t factor_2d = stride_2d / len_2d;

            // Extract size of k-th transfer
            int words_transfer_size = byte_transfer_sizes[k] / sizeof(uint32_t);

            // Allocate source and destination arrays
            int dst_size = words_transfer_size * factor_2d;
            int src_size = words_transfer_size;

            uint32_t src[src_size];
            uint32_t dst[dst_size];

            // fill src array & clear dst array
            for (int i = 0; i < src_size; i++) {
                src[i] = i+1;
            }

            for (int i = 0; i < dst_size; i++) {
                dst[i] = 0;
            }

            dma_src = (int)&src;
            dma_dst = (int)&dst;

            PRINTF("SRC ADDR: %x \n", dma_src);
            PRINTF("DST ADDR: %x \n", dma_dst);
            PRINTF("TRANSFER SIZE: %d \n", byte_transfer_sizes[k]);
            PRINTF("NB_WORDS: %d \n", words_transfer_size);
            PRINTF("STRIDE: %d \n", stride_2d);
            PRINTF("LEN: %d \n", len_2d);

            plp_dma_wait(plp_dma_memcpy_2d(dma_dst, dma_src, byte_transfer_sizes[k], stride_2d, len_2d, 0));

            int j=0;
            // Loop on the number of words moved by the iDMA for the current transfer
            for (int i = 0; i < words_transfer_size; i++) {
                j = i * factor_2d;

                // Check if the data in the destination memory is correct
                if (dst[j] != src[i]) {
                    test_status = 1;
                    PRINTF("ERROR ==> Dst[%d]: %d vs Src[%d]: %d \n", j, dst[j], i, src[i]);
                    errors[k] = errors[k] + 1;
                }
            }

            for (int i = 0; i< dst_size; i++) {
                PRINTF("@%8x: Dst[%d]: %d \n", &dst[i], i, dst[i]);
            }

            // clear both src and dst arrays
            // to prepare for the next transfer
            
            for (int i = 0; i < src_size; i++) {
                src[i] = 0;
            }
            for (int i = 0; i < dst_size; i++) {
                dst[i] = 0;
            }
            PRINTF("Transfer %d finished with %d errors \n", k, errors[k]);
            PRINTF("--------------------------------------------------\n");
        }
        PRINTF ("TEST SUMMARY: \n");
        for (int k = 0; k < NB_TRANSFERS; k++) {
            if (errors[k] != 0) {
                PRINTF("Transfer %d failed with %d errors \n", k, errors[k]);
            } else {
                PRINTF("Transfer %d passed \n", k);
            }
            PRINTF ("--------------------------------------------------\n");
        }
        return test_status;
    }
}
