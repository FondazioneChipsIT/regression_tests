#include "Network.h"

int main() {
    uint32_t tot_err, tot_tested;
    tot_err = 0;
    tot_tested = 0;

    if (rt_core_id() == 0) {
        printf ("ADDER UNTILED TEST STARTING \n");

        DeeployNetwork_input_0 = (int8_t *)pi_l2_malloc(sizeof(int8_t) * 125);
        DeeployNetwork_input_1 = (int8_t *)pi_l2_malloc(sizeof(int8_t) * 125);
        DeeployNetwork_output_0 = (int32_t *)pi_l2_malloc(sizeof(int32_t) * 125);

        printf ("INIT NETWORK \n");
        InitNetwork();

        for (uint32_t buf = 0; buf < DeeployNetwork_num_inputs; buf++) {
            if ((uint32_t)DeeployNetwork_inputs[buf] >= 0x10000000) {
                memcpy(DeeployNetwork_inputs[buf], testInputVector[buf], DeeployNetwork_inputs_bytes[buf]);
            }
        }

        printf ("RUN NETWORK \n");
        RunNetwork();

        void *compbuf;

        for (uint32_t buf = 0; buf < DeeployNetwork_num_outputs; buf++) {
            tot_tested += DeeployNetwork_outputs_bytes[buf] / sizeof(int32_t);
        }
        tot_err = tot_tested;
        printf ("Results to be tested are %d | Error count initialized to %d \n", tot_tested, tot_err);

        for (uint32_t buf = 0; buf < DeeployNetwork_num_outputs; buf++) {

            if ((uint32_t)DeeployNetwork_outputs[buf] < 0x1000000) {
                compbuf = pi_l2_malloc((int)DeeployNetwork_outputs_bytes[buf]);
                // ram_read(compbuf, DeeployNetwork_outputs[buf], DeeployNetwork_outputs_bytes[buf]);
                plp_cl_dma_wait_toL2(plp_cl_dma_l1ToExt((unsigned int) compbuf, (unsigned int) DeeployNetwork_outputs[buf], DeeployNetwork_outputs_bytes[buf]));
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
                } else {
                    tot_err--;
                }
            }
            if ((uint32_t)DeeployNetwork_outputs[buf] < 0x1000000) {
                pi_l2_free(compbuf, (int)DeeployNetwork_outputs_bytes[buf]);
            }

            // Free the previously allocated memory regions
            
            pi_l2_free(DeeployNetwork_input_0, sizeof(int8_t) * 125);
            pi_l2_free(DeeployNetwork_input_1, sizeof(int8_t) * 125);
            pi_l2_free(DeeployNetwork_output_0, sizeof(int8_t) * 125);
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