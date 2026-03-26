#include <stdio.h>
#include <stdlib.h>
#include "pulp.h"

#define SHARED_ADDR ARCHI_CLUSTER_ADDR
#define SIZE 1024

#define L2_BASE ARCHI_L2_SHARED_ADDR

int main() {

  volatile int * p_reg1;
  int error;

  if (rt_core_id() == 0) {
    printf("ADDRESSABILITY TEST - start!\n");
  }

  if(rt_core_id() == 0) {
    pulp_write32(SHARED_ADDR, 0);
    pulp_write32(SHARED_ADDR, pulp_read32(SHARED_ADDR) + 1);
  }

  synch_barrier();

  while(pulp_read32(SHARED_ADDR) < rt_core_id());

  if(rt_core_id() != 0) {
  pulp_write32(SHARED_ADDR,pulp_read32(SHARED_ADDR)+1);
  }

  synch_barrier();

  if(rt_core_id() == 0){
    // Initialize memory
    for(int i=0;i<SIZE;i++)
      pulp_write32(L2_BASE + 0x8000 + i*0x4, i*0x4);

    // TEST CHECK //
    error = 0;
    p_reg1 = (int *) L2_BASE;
    // Read L2
    for(int i=0;i<SIZE;i++){
      p_reg1 = (int *)(L2_BASE + 0x8000 + i*4);
      if( *p_reg1 != i*4){
        error++;
      }
    }
  }

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

  return 0;
}
