
#ifndef _SIMPLECNN_TILED_HEADER_
#define _SIMPLECNN_TILED_HEADER_

#include "XpulpV2/32bit/include/pulp_nn_utils.h"
#include "XpulpV2/32bit/include/pulp_nn_kernels.h"

#include "pulp.h"

#include "stdint.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

extern int8_t *DeeployNetwork_MEMORYARENA_L1;
extern uint32_t DeeployNetwork_MEMORYARENA_L1_len;
extern int8_t *DeeployNetwork_MEMORYARENA_L2;
extern uint32_t DeeployNetwork_MEMORYARENA_L2_len;
extern int8_t *DeeployNetwork_input_0;
extern uint32_t DeeployNetwork_input_0_len;
extern uint8_t *DeeployNetwork_output_0;
extern uint32_t DeeployNetwork_output_0_len;
extern uint32_t DeeployNetwork_num_inputs;
extern uint32_t DeeployNetwork_num_outputs;
extern void *DeeployNetwork_inputs[1];
extern void *DeeployNetwork_outputs[1];
extern uint32_t DeeployNetwork_inputs_bytes[1];
extern uint32_t DeeployNetwork_outputs_bytes[1];

int8_t *DeeployNetwork_MEMORYARENA_L1;
int8_t *DeeployNetwork_MEMORYARENA_L2;
int8_t *DeeployNetwork_input_0;
uint8_t *DeeployNetwork_output_0;

void InitNetwork();
void RunNetwork();

extern void *testInputVector[1];
extern void *testOutputVector[1];

#define OUTPUT_TYPE uint8_t
#define ISOUTPUTFLOAT 0
typedef struct {
  int8_t *DeeployNetwork_TILING_CODEGEN_L1__MERGE_CONVRQ_PASS_0_input_0_transpose_data_in_ref;
  int8_t *DeeployNetwork_TILING_CODEGEN_L1__MERGE_CONVRQ_PASS_0_input_0_transpose_data_out_ref;
} __MERGE_CONVRQ_PASS_0_input_0_transpose_tiling_closure_args_t;

#endif  //  _SIMPLECNN_TILED_HEADER_
