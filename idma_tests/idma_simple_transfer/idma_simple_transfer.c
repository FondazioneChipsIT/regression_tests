#include "idma_simple_transfer.h"

#define MAX_BUFFER_SIZE 0x2200
L1_DATA src[MAX_BUFFER_SIZE];
L2_DATA dst[MAX_BUFFER_SIZE];

int idma_simple_transfer (int k, int ext2loc) {
  int errors = 0;
  uint32_t dma_src_start_addr;
  uint32_t dma_dst_start_addr;

  PRINTF ("Transfer %d \n", k);
  PRINTF ("Number of words to be transferred: %d \n", nb_words[k]);

  // Put the starting addresses of the source and destination spaces in these variables
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

  plp_dma_wait(plp_dma_memcpy(dma_dst_start_addr, dma_src_start_addr, nb_words[k] * sizeof(uint32_t), ext2loc));

  // Loop on the number of words moved by the iDMA for the current transfer
  for (int i = 0; i < nb_words[k]; i++) {
    src_addr = (uint32_t *)((uint8_t *)dma_src_start_addr+ i * sizeof(uint32_t));
    dst_addr = (uint32_t *)((uint8_t *)dma_dst_start_addr+ i * sizeof(uint32_t));
    if (*dst_addr != *src_addr) {
      PRINTF("ERROR ==> Dst[%d]: %d vs Src[%d]: %d \n", i, *dst_addr, i, *src_addr);
      errors++;
    }
  }

  // Clear both the source and the destination arrays to avoid issues with the following transfer
  for (int i = 0; i < nb_words[k]; i++) {
    src_addr = (uint32_t *)((uint8_t *)dma_src_start_addr+ i * sizeof(uint32_t));
    *src_addr = 0;
  }

  for (int i = 0; i < nb_words[k]; i++) {
    dst_addr = (uint32_t *)((uint8_t *)dma_dst_start_addr + i * sizeof(uint32_t));
    *dst_addr = 0;
  }

  return errors;
}

int main() {

  uint32_t errors[NB_TRANSFERS] = {0};
  int test_status = 0;

  if (TEST_ALL_CORES) {
    for (int core=0; core<ARCHI_CLUSTER_NB_PE; core++) {
      if (get_core_id() == core){
        PRINTF("Testing iDMA with cluster core %d\n", core);
        
        for (int k = 0; k < NB_TRANSFERS; k++) {
          /* Local memory to external */
          errors[k] += idma_simple_transfer(k, 0);
          /* External memory to local */
          errors[k] += idma_simple_transfer(k, 1);
          if (errors[k] != 0) {
            test_status = 1;
          }
          PRINTF("Transfer %d finished with %d errors \n", k, errors[k]);
          PRINTF("--------------------------------------------------\n");
        }
      }
      synch_barrier();
    }
  } else {
    if (get_core_id() == 0){
      PRINTF("Testing iDMA with cluster core 0\n");
      // Loop on the number of simple transfers to be executed by the iDMA (specified in idma_defines.h)
      for (int k = 0; k < NB_TRANSFERS; k++) {
        /* Local memory to external */
        errors[k] += idma_simple_transfer(k, 0);
        /* External memory to local */
        errors[k] += idma_simple_transfer(k, 1);
        if (errors[k] != 0) {
          test_status = 1;
        }
        PRINTF("Transfer %d finished with %d errors \n", k, errors[k]);
        PRINTF("--------------------------------------------------\n");
      }
    }
  }
  return test_status;
}
