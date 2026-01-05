#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include "pulp.h"
#include "stdint.h"
// #include "pulp_nn_kernels.h"
// #include "DeeployBasicMath.h"
// #include "DeeployPULPMath.h"
// #include "bsp/ram.h"
// #include "pulp_core.h"
#include "Network.h"

int main() {
    uint32_t tot_err, tot_tested;
    tot_err = 0;
    tot_tested = 0;
    if (rt_core_id() == 0) {
        printf ("Starting Deeploy test \n");
        printf ("Before Initnetwork \n");
        InitNetwork();
        printf ("After Initnetwork \n");
        for (uint32_t buf = 0; buf < DeeployNetwork_num_inputs; buf++) {
            if ((uint32_t)DeeployNetwork_inputs[buf] >= 0x10000000) {
                memcpy(DeeployNetwork_inputs[buf], testInputVector[buf], DeeployNetwork_inputs_bytes[buf]);
            }
        }
        printf ("Before RunNetwork \n");
        RunNetwork();
        printf ("After RunNetwork \n");

        void *compbuf;

        for (uint32_t buf = 0; buf < DeeployNetwork_num_outputs; buf++) {
            tot_tested += DeeployNetwork_outputs_bytes[buf] / sizeof(int32_t);
        }

        for (uint32_t buf = 0; buf < DeeployNetwork_num_outputs; buf++) {
            tot_tested += DeeployNetwork_outputs_bytes[buf] / sizeof(int32_t);

            if ((uint32_t)DeeployNetwork_outputs[buf] < 0x1000000) {
                compbuf = pi_l2_malloc((int)DeeployNetwork_outputs_bytes[buf]);
                // ram_read(compbuf, DeeployNetwork_outputs[buf], DeeployNetwork_outputs_bytes[buf]);
                plp_cl_dma_wait_toL2(plp_cl_dma_l1ToExt(&compbuf, &DeeployNetwork_outputs[buf], DeeployNetwork_outputs_bytes[buf]));
            } else {
                compbuf = DeeployNetwork_outputs[buf];
            }

            for (uint32_t i = 0; i < DeeployNetwork_outputs_bytes[buf] / sizeof(int32_t); i++) {
                int32_t expected = ((int32_t *)testOutputVector[buf])[i];
                int32_t actual = ((int32_t *)compbuf)[i];
                int32_t error = expected - actual;
                int32_t diff = (int32_t)(error < 0 ? -error : error);

                if (diff) {
                    printf ("Difference found \n");
                    tot_err++;
                }
            }
            if ((uint32_t)DeeployNetwork_outputs[buf] < 0x1000000) {
                pi_l2_free(compbuf, (int)DeeployNetwork_outputs_bytes[buf]);
            }
        }
    }

    synch_barrier();

    printf("Writing to mailbox...\n");
    pulp_write32(0x10404008, (tot_err));
    pulp_write32(0x10404020, 0x1);

    synch_barrier();
    return 0;
}