#include "idma_simple_transfer.h"

int main() {

  uint32_t errors[NB_TRANSFERS] = {0};
  int test_status = 0;
  // Executes only on Core 0
  if (rt_core_id() == 0) {

    // Loop on the number of simple transfers to be executed by the iDMA (specified in idma_defines.h)
    for (int k = 0; k < NB_TRANSFERS; k++) {

      uint32_t dma_src;
      uint32_t dma_dst;

      // Allocate source and destination arrays
      uint32_t src[byte_transfer_sizes[k] / sizeof(uint32_t)];
      uint32_t dst[byte_transfer_sizes[k] / sizeof(uint32_t)];

      // fill src array & clear dst array
      for (int i = 0; i < (byte_transfer_sizes[k] / sizeof(uint32_t)); i++) {
        src[i] = i;
        dst[i] = 0;
      }

      dma_src = (int)&src;
      dma_dst = (int)&dst;

      PRINTF("SRC ADDR: %x \n", dma_src);
      PRINTF("DST ADDR: %x \n", dma_dst);
      PRINTF("TRANSFER SIZE: %d \n", byte_transfer_sizes[k]);
      PRINTF("NB_ELEMENTS: %d \n", byte_transfer_sizes[k] / sizeof(uint32_t));

      plp_dma_wait(plp_dma_memcpy(dma_dst, dma_src, byte_transfer_sizes[k], 0));

      // Loop on the number of words moved by the iDMA for the current transfer
      for (int i = 0; i < (byte_transfer_sizes[k] / sizeof(uint32_t)); i++) {
        if (dst[i] != src[i]) {
          test_status = 1;
          PRINTF("ERROR ==> Dst[%d]: %d vs Src[%d]: %d \n", i, dst[i], i, src[i]);
          errors[k] = errors[k] + 1;
        }
      }
      // clear both src and dst arrays
      // to prepare for the next transfer
      // this is not necessary, but it makes debugging easier
      // and allows to see the errors in the next transfer
            
      for (int i = 0; i < (byte_transfer_sizes[k] / sizeof(uint32_t)); i++) {
        src[i] = 0;
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
