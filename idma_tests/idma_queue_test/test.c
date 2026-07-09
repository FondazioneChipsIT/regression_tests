#include "test.h"

int errors = 0;
uint32_t l1_addr[TRANSFERS_QUEUE] = {0};
uint32_t l2_addr[TRANSFERS_QUEUE] = {0};

// Tests a L2 -> L1 transfer
int test_idma_rx (int core_id, int size) {
    PRINTF ("RX TESTCASE --> Transfer from L2 to L1 \n");
    volatile uint8_t *src_ptr[TRANSFERS_QUEUE];
    volatile uint8_t *dst_ptr[TRANSFERS_QUEUE];
    int error = 0;

    for (int i=0; i<TRANSFERS_QUEUE; i++) {
        // L2 to L1 transfer
        src_ptr[i] = (uint8_t*) l2_addr[i];
        dst_ptr[i] = (uint8_t*) l1_addr[i];        
    }

    for (int t = 0; t < TRANSFERS_QUEUE; t++) {
        for (int i = 0; i < size; i++) {
            src_ptr[t][i] = (uint8_t)((i + t) & 0xFF);
            dst_ptr[t][i] = (uint8_t)((i + t - 1) & 0xFF);
        }
    }

    plp_idma_enable_clk();
    reset_cycle_count();
    start_cycle_count();
    for (int i=0; i<TRANSFERS_QUEUE; i++) {
        pulp_cl_idma_L2ToL1_cfg((unsigned int) src_ptr[i], (unsigned int) dst_ptr[i], size);
    }
    for (int i=0; i<TRANSFERS_QUEUE; i++) {
        pulp_cl_idma_L2ToL1_launch();
    }
    plp_cl_dma_barrier_toL1();
    stop_cycle_count();
    plp_idma_disable_clk();

    // Check the results

    #ifdef DISABLE_ERROR_CHECK
    error = 0;
    #else
    for (int t = 0; t < TRANSFERS_QUEUE; t++) {
        for (int i = 0; i < size; i++) {
            uint8_t expected = src_ptr[t][i];
            uint8_t actual   = dst_ptr[t][i];

            if (expected != actual) {
                error++;

                if (core_id == 0) {
                    PRINTF("Error: buffer %d, byte %d: expected @%p = 0x%02x, actual @%p = 0x%02x\n",
                        t, i,
                        (void *)&src_ptr[t][i], expected,
                        (void *)&dst_ptr[t][i], actual);
                }
            }
        }
    }
    #endif

    return error;
}

// Tests a L1 -> L2 transfer
int test_idma_tx (int core_id, int size) {
    PRINTF ("TX TESTCASE --> Transfer from L1 to L2 \n");
    volatile uint8_t *src_ptr[TRANSFERS_QUEUE];
    volatile uint8_t *dst_ptr[TRANSFERS_QUEUE];
    int error = 0;

    for (int i=0; i<TRANSFERS_QUEUE; i++) {
        // L2 to L1 transfer
        src_ptr[i] = (uint8_t*) l1_addr[i];
        dst_ptr[i] = (uint8_t*) l2_addr[i];        
    }

    for (int t = 0; t < TRANSFERS_QUEUE; t++) {
        for (int i = 0; i < size; i++) {
            src_ptr[t][i] = (uint8_t)((i + t) & 0xFF);
            dst_ptr[t][i] = (uint8_t)((i + t - 1) & 0xFF);
        }
    }

    plp_idma_enable_clk();
    reset_cycle_count();
    start_cycle_count();
    for (int i=0; i<TRANSFERS_QUEUE; i++) {
        pulp_cl_idma_L1ToL2_cfg((unsigned int) src_ptr[i], (unsigned int) dst_ptr[i], size);
    }
    for (int i=0; i<TRANSFERS_QUEUE; i++) {
        pulp_cl_idma_L1ToL2_launch();
    }
    plp_cl_dma_barrier_toL2();
    stop_cycle_count();
    plp_idma_disable_clk();

    // Check the results

    #ifdef DISABLE_ERROR_CHECK
    error = 0;
    #else
    for (int t = 0; t < TRANSFERS_QUEUE; t++) {
        for (int i = 0; i < size; i++) {
            uint8_t expected = src_ptr[t][i];
            uint8_t actual   = dst_ptr[t][i];

            if (expected != actual) {
                error++;

                if (core_id == 0) {
                    PRINTF("Error: buffer %d, byte %d: expected @%p = 0x%02x, actual @%p = 0x%02x\n",
                        t, i,
                        (void *)&src_ptr[t][i], expected,
                        (void *)&dst_ptr[t][i], actual);
                }
            }
        }
    }
    #endif

    return error;
}

void allocate_mem_to_cores () {
    int core_id = rt_core_id();

    if (core_id == 0) {
        for (int i=0; i<TRANSFERS_QUEUE; i++) {
            l1_addr[i]     = (uint32_t) pi_l1_malloc(0, CORE_SPACE);
            l2_addr[i]     = (uint32_t) pi_l2_malloc(CORE_SPACE);
        }
    }

    // The following synch_barrier is needed so that
    // no core can assign its address range until the mallocs are executed
    synch_barrier();

    if (core_id == 0) {
        PRINTF ("Addresses for enqueued transfers \n");
        for (int i=0; i<TRANSFERS_QUEUE; i++) {
            PRINTF ("Queue idx: %d \n", i);
            PRINTF ("l1_addr = %8x \n", l1_addr[i]);
            PRINTF ("l2_addr = %8x \n", l2_addr[i]);
        }
    }

    // The following synch_barrier is needed so that
    // no core can start executing until all address ranges have been assigned
    synch_barrier();

}

void free_allocated_memory () {

    synch_barrier();
    // Only Core 0 takes care of freeing the allocated memory, since it's the one
    // allocated it at the beginning of the test
    if (core_id == 0) {
        for (int i=0; i<TRANSFERS_QUEUE; i++) {
            pi_l1_free(0, l1_addr[i], CORE_SPACE);
            pi_l2_free(l2_addr[i], CORE_SPACE);
        }
    }
}

int cluster_task () {
    int core_id = rt_core_id();
    unsigned int size = TRANSFER_SIZE;

    allocate_mem_to_cores();

    if (core_id == 0){
        #ifdef TX
        errors += test_idma_tx(core_id, size);
        #endif
        #ifdef RX
        errors += test_idma_rx(core_id, size);
        #endif
        #ifdef IDLE
        errors += test_idma_idle(size);
        #endif
    }

    synch_barrier();
    free_allocated_memory();

    return errors;
}

int main () {
    int retval = 1;
    retval = cluster_task();
    return retval;
}