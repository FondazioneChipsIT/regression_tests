
#ifndef _ADDER_TILED_HEADER_
#define _ADDER_TILED_HEADER_

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>

#include "pulp.h"

extern int8_t *DeeployNetwork_MEMORYARENA_L1;
extern uint32_t DeeployNetwork_MEMORYARENA_L1_len;
extern int8_t *DeeployNetwork_MEMORYARENA_L2;
extern uint32_t DeeployNetwork_MEMORYARENA_L2_len;
extern int8_t *DeeployNetwork_input_0;
extern uint32_t DeeployNetwork_input_0_len;
extern int8_t *DeeployNetwork_input_1;
extern uint32_t DeeployNetwork_input_1_len;
extern int32_t *DeeployNetwork_output_0;
extern uint32_t DeeployNetwork_output_0_len;
extern uint32_t DeeployNetwork_num_inputs;
extern uint32_t DeeployNetwork_num_outputs;
extern void *DeeployNetwork_inputs[2];
extern void *DeeployNetwork_outputs[1];
extern uint32_t DeeployNetwork_inputs_bytes[2];
extern uint32_t DeeployNetwork_outputs_bytes[1];


extern int8_t *DeeployNetwork_MEMORYARENA_L1;
extern int8_t *DeeployNetwork_MEMORYARENA_L2;
extern int8_t *DeeployNetwork_input_0;
extern int8_t *DeeployNetwork_input_1;
extern int32_t *DeeployNetwork_output_0;

extern uint8_t DeeployNetwork_TILING_CODEGEN_L1_Add_numTiles[2];

extern void *DeeployNetwork_inputs[2];
extern void *DeeployNetwork_outputs[1];

extern void* testInputVector[2];
extern void* testOutputVector[1];

void InitNetwork();
void RunNetwork();

typedef struct {
  int8_t *DeeployNetwork_TILING_CODEGEN_L1_Add_data_in_1_ref;
  int8_t *DeeployNetwork_TILING_CODEGEN_L1_Add_data_in_2_ref;
  int32_t *DeeployNetwork_TILING_CODEGEN_L1_Add_data_out_ref;
} _Add_tiling_closure_args_t;

#define OUTPUTTYPE int32_t
#define ISOUTPUTFLOAT 0

#endif  //  _ADDER_TILED_HEADER_
