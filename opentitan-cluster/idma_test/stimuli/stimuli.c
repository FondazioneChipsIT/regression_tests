#include <stdio.h>
#include <stdlib.h>
#include "pulp.h"

#define SHARED_ADDR 0xB0000000
#define SIZE 1024

#define L2_BASE 0x1C001000

int main() {

  int error;

  if(core_id() == 0) {
    pulp_write32(SHARED_ADDR, 0);
    pulp_write32(SHARED_ADDR, pulp_read32(SHARED_ADDR) + 1);
  }

  synch_barrier();

  while(pulp_read32(SHARED_ADDR) < core_id());

  if(core_id() != 0) {
    pulp_write32(SHARED_ADDR,pulp_read32(SHARED_ADDR)+1);
  }

  synch_barrier();

  if(core_id() == 0){
    error = 0;

    // initialize external L2 memory
    for(int i=0;i<SIZE;i++)
      pulp_write32(L2_BASE + 0x8000 + i*0x4, i*0x4);

    // copy from external L2 to cluster L1
    int id1 = pulp_cl_idma_L2ToL1(L2_BASE + 0x8000, SHARED_ADDR, SIZE);
    plp_cl_dma_barrier_toL1();

    // copy from cluster L1 to external L2
    // int id2 = pulp_cl_idma_L1ToL2(SHARED_ADDR, L2_BASE + 0xB000, SIZE);
    // plp_cl_dma_wait_toL2(id2);

  }

  return 0;
}
