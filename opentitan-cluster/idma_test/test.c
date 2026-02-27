#include <stdio.h>
#include <stdlib.h>
#include "pulp.h"

#define SIZE 1024

uint32_t l1_addr[8] = {0};
uint32_t l2_addr[8] = {0};

int main() {

  int error;
  volatile uint8_t *src_ptr, *dst_ptr;
  int transfer_size = SIZE/2;

  synch_barrier();

  if (rt_core_id() == 0) {
    printf("IDMA TEST - start!\n");
  }

  // Allocate memory regions in L1 and L2 for iDMA testing
  if (rt_core_id() == 0) {
        l1_addr[0]     = (uint32_t) pi_l1_malloc(0, SIZE);
        l2_addr[0]     = (uint32_t) pi_l2_malloc(SIZE);
  }

  if(rt_core_id() == 0){
    error = 0;

    src_ptr = (uint8_t*) l2_addr[0];
    dst_ptr = (uint8_t*) l1_addr[0];

    // Fill the source and destination regions with testing data
    for (int i = 0; i < transfer_size; i++) {
        src_ptr[i] = (uint8_t)(i & 0xFF);
        dst_ptr[i] = (uint8_t)(i-1 & 0xFF);
    }

    // copy from external L2 to cluster L1
    printf("STARTING TRANSFER FROM L2 TO L1...\n");
    plp_cl_dma_wait_toL1(pulp_cl_idma_L2ToL1((unsigned int) src_ptr, (unsigned int) dst_ptr, transfer_size));

    for (int i=0; i < transfer_size; i++) {
      uint8_t expected = src_ptr[i];
      uint8_t actual   = dst_ptr[i];

      if (expected != actual) {
        error++;
        printf ("Error: expected @%8x = %8x vs actual @%8x = %8x \n", expected, &src_ptr[i], actual, &dst_ptr[i]);
      }
    }

    src_ptr = (uint8_t*) l1_addr[0];
    dst_ptr = (uint8_t*) l2_addr[0];

    // Fill the source and destination regions with testing data
    for (int i = 0; i < transfer_size; i++) {
        src_ptr[i] = (uint8_t)(i & 0xFF);
        dst_ptr[i] = (uint8_t)(i-1 & 0xFF);
    }

    // copy from cluster L1 to external L2
    printf("STARTING TRANSFER FROM L1 TO L2...\n");
    plp_cl_dma_wait_toL2(pulp_cl_idma_L1ToL2((unsigned int) src_ptr, (unsigned int) dst_ptr, transfer_size));

    for (int i=0; i < transfer_size; i++) {
      uint8_t expected = src_ptr[i];
      uint8_t actual   = dst_ptr[i];

      if (expected != actual) {
        error++;
        printf ("Error: expected @%8x = %8x vs actual @%8x = %8x \n", expected, &src_ptr[i], actual, &dst_ptr[i]);
      }
    }
  }

  synch_barrier();

  // Free the previously allocated memory regions
  if (rt_core_id() == 0) {
    pi_l1_free(0, l1_addr, SIZE);
    pi_l2_free(l2_addr, SIZE);
  }

  synch_barrier();

  return error;
}
