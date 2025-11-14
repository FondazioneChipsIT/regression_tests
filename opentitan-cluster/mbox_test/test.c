#include <stdio.h>
#include <stdlib.h>
#include "pulp.h"

#define SHARED_ADDR 0xB0000000

#define MboxAddrReg0 0x10404008
#define MboxAddrReg1 0x10404010
#define MboxAddrReg2 0x10404014
#define MboxAddrReg3 0x10404018
#define MboxAddrReg4 0x1040401C
#define MboxAddrReg5 0x10404020

int main() {

  if (rt_core_id() == 0) {
    printf("MBOX TEST - start!\n");
  }

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
    printf("Writing to mailbox...\n");
    pulp_write32(MboxAddrReg0, 0xBAADC0DE);
    pulp_write32(MboxAddrReg1, 0xBAADC0DE);
    pulp_write32(MboxAddrReg2, 0xBAADC0DE);
    pulp_write32(MboxAddrReg3, 0xBAADC0DE);
    pulp_write32(MboxAddrReg4, 0xBAADC0DE);
    pulp_write32(MboxAddrReg5, 0x1); //ring doorbell
  }

  return 0;
}
