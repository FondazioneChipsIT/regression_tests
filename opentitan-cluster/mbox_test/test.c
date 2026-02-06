#include <stdio.h>
#include <stdlib.h>
#include "pulp.h"

#define SHARED_ADDR ARCHI_CLUSTER_ADDR

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
    pulp_write32(MAILBOXES_REG0_OFFSET, 0xBAADC0DE);
    pulp_write32(MAILBOXES_REG1_OFFSET, 0xBAADC0DE);
    pulp_write32(MAILBOXES_REG2_OFFSET, 0xBAADC0DE);
    pulp_write32(MAILBOXES_REG3_OFFSET, 0xBAADC0DE);
    pulp_write32(MAILBOXES_REG4_OFFSET, 0xBAADC0DE);
    pulp_write32(MAILBOXES_REG5_OFFSET, 0x1); //ring doorbell
  }

  return 0;
}
