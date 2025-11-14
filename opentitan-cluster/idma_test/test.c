#include <stdio.h>
#include <stdlib.h>
#include "pulp.h"

#define SHARED_ADDR 0xB0000000
#define SIZE 1024

#define L2_BASE 0x1C001000

int main() {

  int error;
<<<<<<< HEAD
  volatile uint8_t *src_ptr, *dst_ptr;
  int transfer_size = SIZE/2;

  synch_barrier();

=======

  if(core_id() == 0) {
    pulp_write32(SHARED_ADDR, 0);
    pulp_write32(SHARED_ADDR, pulp_read32(SHARED_ADDR) + 1);
  }

  synch_barrier();

<<<<<<<< HEAD:opentitan-cluster/idma_test/stimuli/stimuli.c
  while(pulp_read32(SHARED_ADDR) < core_id());

  if(core_id() != 0) {
    pulp_write32(SHARED_ADDR,pulp_read32(SHARED_ADDR)+1);
========
>>>>>>> 07aeab2 (Some test cleanup + add several printf)
  if (rt_core_id() == 0) {
    printf("IDMA TEST - start!\n");
  }

  // Allocate memory regions in L1 and L2 for iDMA testing
  if (rt_core_id() == 0) {
        l1_addr[0]     = (uint32_t) pi_l1_malloc(0, SIZE);
        l2_addr[0]     = (uint32_t) pi_l2_malloc(SIZE);
<<<<<<< HEAD
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
=======
>>>>>>>> 07aeab2 (Some test cleanup + add several printf):opentitan-cluster/idma_test/test.c
  }

  synch_barrier();

  if(core_id() == 0){
    error = 0;

    // initialize external L2 memory
    for(int i=0;i<SIZE;i++)
      pulp_write32(L2_BASE + 0x8000 + i*0x4, i*0x4);

    // copy from external L2 to cluster L1
<<<<<<<< HEAD:opentitan-cluster/idma_test/stimuli/stimuli.c
    int id1 = pulp_cl_idma_L2ToL1(L2_BASE + 0x8000, SHARED_ADDR, SIZE);
    plp_cl_dma_barrier_toL1();

    // copy from cluster L1 to external L2
    // int id2 = pulp_cl_idma_L1ToL2(SHARED_ADDR, L2_BASE + 0xB000, SIZE);
    // plp_cl_dma_wait_toL2(id2);
========
>>>>>>> 07aeab2 (Some test cleanup + add several printf)
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
<<<<<<< HEAD

    for (int i=0; i < transfer_size; i++) {
      uint8_t expected = src_ptr[i];
      uint8_t actual   = dst_ptr[i];

      if (expected != actual) {
        error++;
        printf ("Error: expected @%8x = %8x vs actual @%8x = %8x \n", expected, &src_ptr[i], actual, &dst_ptr[i]);
      }
    }
  }

=======
>>>>>>>> 07aeab2 (Some test cleanup + add several printf):opentitan-cluster/idma_test/test.c

  }

<<<<<<<< HEAD:opentitan-cluster/idma_test/stimuli/stimuli.c
========
>>>>>>> 07aeab2 (Some test cleanup + add several printf)
  synch_barrier();

  // Free the previously allocated memory regions
  if (rt_core_id() == 0) {
    pi_l1_free(0, l1_addr, SIZE);
    pi_l2_free(l2_addr, SIZE);
  }

<<<<<<< HEAD
  synch_barrier();

  return error;
=======
  if (rt_core_id() == 0) {

    if(error == 0){
      printf("TEST PASSED!\n");
    } else {
      printf("TEST FAILED!\n");
    }

    printf("Writing to mailbox...\n");
    pulp_write32(0x10404008, error);
    pulp_write32(0x10404020, 0x1);
  }

  synch_barrier();

>>>>>>>> 07aeab2 (Some test cleanup + add several printf):opentitan-cluster/idma_test/test.c
  return 0;
>>>>>>> 07aeab2 (Some test cleanup + add several printf)
}
