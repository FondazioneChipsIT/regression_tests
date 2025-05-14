#include "idma_simple_transfer.h"

#define MAX_BUFFER_SIZE 500
L1_DATA src[MAX_BUFFER_SIZE];
L1_DATA src2[MAX_BUFFER_SIZE];
L2_DATA dst[MAX_BUFFER_SIZE];

int idma_simple_transfer (int k, int ext2loc, int loc2loc) {
  int errors = 0;
  uint32_t dma_src_start_addr;
  uint32_t dma_src_2_start_addr;
  uint32_t dma_dst_start_addr;

  PRINTF ("Transfer %d \n", k);
  PRINTF ("Number of words to be transferred: %d \n", nb_words[k]);

  // Put the starting addresses of the source and destination spaces in these variables
  dma_src_start_addr    = (int)&src;
  dma_src_2_start_addr  = (int)&src2;
  dma_dst_start_addr    = (int)&dst;

  uint32_t *src_addr;
  uint32_t *src_2_addr;
  uint32_t *dst_addr;

  int transfer_id;

  // Fill src array with test data
  for (int i = 0; i < nb_words[k]; i++) {
    src_addr = (uint32_t *)((uint8_t *)dma_src_start_addr + i * sizeof(uint32_t));
    *src_addr = i+1;
  }

  // Clear the src2 array: Works as destination array for the L1 to L1 transfer --> ONGOING
  for (int i = 0; i < nb_words[k]; i++) {
    src_2_addr = (uint32_t *)((uint8_t *)dma_src_2_start_addr + i * sizeof(uint32_t));
    *src_2_addr = 0;
  }

  // Clear the destination array
  for (int i = 0; i < nb_words[k]; i++) {   
    dst_addr = (uint32_t *)((uint8_t *)dma_dst_start_addr + i * sizeof(uint32_t));
    *dst_addr = nb_words[k]-i;
  }

  if (loc2loc) {
    plp_dma_wait(pulp_idma_L1ToL1(dma_src_2_start_addr, dma_src_start_addr, nb_words[k] * sizeof(uint32_t)));
    // Loop on the number of words moved by the iDMA for the current transfer
    for (int i = 0; i < nb_words[k]; i++) {
      src_addr = (uint32_t *)((uint8_t *)dma_src_start_addr+ i * sizeof(uint32_t));
      src_2_addr = (uint32_t *)((uint8_t *)dma_src_2_start_addr+ i * sizeof(uint32_t));
      if (*src_2_addr != *src_addr) {
        PRINTF("ERRORS ==> @%8x L1[%d]: %d vs @%8x L1[%d]: %d \n", src_2_addr, i, *src_2_addr, src_addr, i, *src_addr);
        errors++;
      }
    }
  } else {
    plp_dma_memcpy(dma_dst_start_addr, dma_src_start_addr, nb_words[k] * sizeof(uint32_t), ext2loc);
  }
  plp_dma_barrier();

  // Loop on the number of words moved by the iDMA for the current transfer
  for (int i = 0; i < nb_words[k]; i++) {
    src_addr = (uint32_t *)((uint8_t *)dma_src_start_addr+ i * sizeof(uint32_t));
    dst_addr = (uint32_t *)((uint8_t *)dma_dst_start_addr+ i * sizeof(uint32_t));
    if (*dst_addr != *src_addr) {
      PRINTF("ERRORS ==> @%8x L2[%d]: %d vs @%8x L1[%d]: %d \n", dst_addr, i, *dst_addr, src_addr, i, *src_addr);
      errors++;
    }
  }

  // Clear both the source arrays and the destination array to avoid issues with the following transfer
  for (int i = 0; i < nb_words[k]; i++) {
    src_addr = (uint32_t *)((uint8_t *)dma_src_start_addr+ i * sizeof(uint32_t));
    *src_addr = 0;
  }

  for (int i = 0; i < nb_words[k]; i++) {
    src_2_addr = (uint32_t *)((uint8_t *)dma_src_2_start_addr+ i * sizeof(uint32_t));
    *src_2_addr = 0;
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
          /* External memory to local --> L2 to L1 */
          PRINTF("Transfer L2 -> L1 %d finished with %d errors \n", k, errors[k]);
          PRINTF("--------------------------------------------------\n");
          errors[k] += idma_simple_transfer(k, 1, 0);
          /* Local memory to external --> L1 to L2 */
          PRINTF("Transfer  L1 -> L2 %d finished with %d errors \n", k, errors[k]);
          PRINTF("--------------------------------------------------\n");
          errors[k] += idma_simple_transfer(k, 0, 0);
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
        /* Local memory to external --> L1 to L2 */
        errors[k] += idma_simple_transfer(k, 0, 0);
        PRINTF("Transfer  L1 -> L2 %d finished with %d errors \n", k, errors[k]);
        PRINTF("--------------------------------------------------\n");
        /* External memory to local --> L2 to L1 */
        errors[k] += idma_simple_transfer(k, 1, 0);
        PRINTF("Transfer L2 -> L1 %d finished with %d errors \n", k, errors[k]);
        PRINTF("--------------------------------------------------\n");
        // /* Local memory to local --> L1 to L1 */
        // errors[k] += idma_simple_transfer(k, 0, 1);
        // PRINTF("Transfer L1 -> L1 %d finished with %d errors \n", k, errors[k]);
        // PRINTF("--------------------------------------------------\n");
        if (errors[k] != 0) {
          test_status = 1;
        }
      }
    }
  }
  return test_status;
}
