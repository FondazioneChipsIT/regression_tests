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
    hal_write_to_mailbox(ARCHI_MAILBOX_LETTER0_OFFSET, 0xBAADC0DE);
    hal_write_to_mailbox(ARCHI_MAILBOX_LETTER1_OFFSET, 0xBAADC0DE);
  }

  return 0;
}
