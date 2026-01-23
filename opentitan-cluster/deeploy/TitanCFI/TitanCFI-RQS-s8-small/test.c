#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include "pulp.h"
#include "stdint.h"
#include "Network.h"

int main() {
    uint32_t tot_err, tot_tested;
    tot_err = 0;
    tot_tested = 0;

    if (rt_core_id() == 0) {
        printf ("Starting Deeploy test \n");
        printf ("Initnetwork \n");
        InitNetwork();

        for (uint32_t buf = 0; buf < DeeployNetwork_num_inputs; buf++) {
            if ((uint32_t)DeeployNetwork_inputs[buf] >= 0x10000000) {
                memcpy(DeeployNetwork_inputs[buf], testInputVector[buf], DeeployNetwork_inputs_bytes[buf]);
            }
        }
    }

    synch_barrier();
    if (rt_core_id() == 0) {
        printf ("RunNetwork \n");
    }
    RunNetwork();
    synch_barrier();

    if (rt_core_id() == 0) {
        void *compbuf;

        for (uint32_t buf = 0; buf < DeeployNetwork_num_outputs; buf++) {
            tot_tested += DeeployNetwork_outputs_bytes[buf] / sizeof(OUTPUT_TYPE);
        }

        tot_err = tot_tested;
        printf ("Tot err is initialized to %d \n", tot_err);
        for (uint32_t buf = 0; buf < DeeployNetwork_num_outputs; buf++) {

            if ((uint32_t)DeeployNetwork_outputs[buf] < 0x1000000) {
                compbuf = pi_l2_malloc((int)DeeployNetwork_outputs_bytes[buf]);
                plp_cl_dma_wait_toL2(plp_cl_dma_l1ToExt((unsigned int) compbuf, (unsigned int) DeeployNetwork_outputs[buf], DeeployNetwork_outputs_bytes[buf]));
            } else {
                compbuf = DeeployNetwork_outputs[buf];
            }

            for (uint32_t i = 0; i < DeeployNetwork_outputs_bytes[buf] / sizeof(OUTPUT_TYPE); i++) {
                OUTPUT_TYPE expected = ((OUTPUT_TYPE *)testOutputVector[buf])[i];
                OUTPUT_TYPE actual = ((OUTPUT_TYPE *)compbuf)[i];
                int32_t error = expected - actual;
                OUTPUT_TYPE diff = (OUTPUT_TYPE)(error < 0 ? -error : error);

                if (diff) {
                    printf ("Difference found \n");
                } else {
                    tot_err --;
                }
            }
            if ((uint32_t)DeeployNetwork_outputs[buf] < 0x1000000) {
                pi_l2_free(compbuf, (int)DeeployNetwork_outputs_bytes[buf]);
            }
        }

        if (tot_err == 0) {
            printf ("TEST PASSED!!! \n");
        } else {
            printf ("TEST FAILED!!! \n");
        }
        printf("Writing %d errors to mailbox...\n", tot_err);
        pulp_write32(0x10404008, tot_err);
        pulp_write32(0x10404020, 0x1);
    }

    synch_barrier();
    return 0;
}