#include "idma_simple_transfer.h"

int main() {

  uint32_t errors[NB_TRANSFERS] = {0};
  int test_status = 0;
  // Executes only on Core 0
  if (rt_core_id() == 0) {

    // Loop on the number of simple transfers to be executed by the iDMA (specified in idma_defines.h)
    for (int k = 0; k < NB_TRANSFERS; k++) {

      uint32_t dma_src_start_addr;
      uint32_t dma_dst_start_addr;

      PRINTF ("Transfer %d \n", k);
      PRINTF ("Number of words to be transferred: %d \n", nb_words[k]);

      // Allocate source and destination arrays
      uint32_t src[nb_words[k] * sizeof(uint32_t)];
      uint32_t dst[nb_words[k] * sizeof(uint32_t)];

      dma_src_start_addr = (int)&src;
      dma_dst_start_addr = (int)&dst;

      uint32_t *src_addr;
      uint32_t *dst_addr;

      // Fill src array with test data
      for (int i = 0; i < nb_words[k]; i++) {
        src_addr = (uint32_t *)((uint8_t *)dma_src_start_addr + i * sizeof(uint32_t));
        *src_addr = i+1;
      }

      // Clear the destination array
      for (int i = 0; i < nb_words[k]; i++) {
        dst_addr = (uint32_t *)((uint8_t *)dma_dst_start_addr + i * sizeof(uint32_t));
        *dst_addr = 0;
      }

      plp_dma_wait(plp_dma_memcpy(dma_dst_start_addr, dma_src_start_addr, nb_words[k] * sizeof(uint32_t), 0));

      // Loop on the number of words moved by the iDMA for the current transfer
      for (int i = 0; i < nb_words[k]; i++) {
        src_addr = (uint32_t *)((uint8_t *)dma_src_start_addr+ i * sizeof(uint32_t));
        dst_addr = (uint32_t *)((uint8_t *)dma_dst_start_addr+ i * sizeof(uint32_t));
        if (*dst_addr != *src_addr) {
          test_status = 1;
          PRINTF("ERROR ==> Dst[%d]: %d vs Src[%d]: %d \n", i, *dst_addr, i, *src_addr);
          errors[k] = errors[k] + 1;
      }
      }
      // clear both src and dst arrays
      // to prepare for the next transfer
      // this is not necessary, but it makes debugging easier
      // and allows to see the errors in the next transfer
            
      // Clear both the source and the destination arrays to avoid issues with the following transfer
      for (int i = 0; i < nb_words[k]; i++) {
        src_addr = (uint32_t *)((uint8_t *)dma_src_start_addr+ i * sizeof(uint32_t));
        *src_addr = 0;
      }

      for (int i = 0; i < nb_words[k]; i++) {
        dst_addr = (uint32_t *)((uint8_t *)dma_dst_start_addr + i * sizeof(uint32_t));
        *dst_addr = 0;
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
