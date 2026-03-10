/*
 * Copyright (C) 2018 ETH Zurich and University of Bologna
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <stdio.h>
#include "pulp.h"

#define ARCHI_SOC_MAILBOXES_ADDR   0x40000000

#define ARCHI_MAILBOXES_REG0_OFFSET         0x08
#define ARCHI_MAILBOXES_REG1_OFFSET         0x10
#define ARCHI_MAILBOXES_REG2_OFFSET         0x14
#define ARCHI_MAILBOXES_REG3_OFFSET         0x18
#define ARCHI_MAILBOXES_REG4_OFFSET         0x1C
#define ARCHI_MAILBOXES_REG5_OFFSET         0x20
#define ARCHI_MAILBOXES_DOORBELL            ARCHI_MAILBOXES_REG5_OFFSET
#define ARCHI_MAILBOXES_RETURN              ARCHI_MAILBOXES_REG0_OFFSET

int main()
{

  // generate external mbox irq to wake-up Ibex //
  pulp_write32(ARCHI_SOC_MAILBOXES_ADDR+ARCHI_MAILBOXES_RETURN, 0);
  pulp_write32(ARCHI_SOC_MAILBOXES_ADDR+ARCHI_MAILBOXES_DOORBELL, 1);

  printf("Hello !\n");

  return 0;
}
