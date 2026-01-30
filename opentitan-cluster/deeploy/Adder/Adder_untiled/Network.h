
#ifndef _ADDER_UNTILED_HEADER_
#define _ADDER_UNTILED_HEADER_

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include "pulp.h"
#include "stdint.h"


extern int8_t *DeeployNetwork_input_0;
extern int8_t *DeeployNetwork_input_1;
extern int32_t *DeeployNetwork_output_0;
extern const uint32_t DeeployNetwork_num_inputs;
extern const uint32_t DeeployNetwork_num_outputs;
extern void* DeeployNetwork_inputs[2];
extern void* DeeployNetwork_outputs[1];
extern const uint32_t DeeployNetwork_inputs_bytes[2];
extern const uint32_t DeeployNetwork_outputs_bytes[1];

extern void* testInputVector[2];
extern void* testOutputVector[1];

extern void* DeeployNetwork_inputs[2];
extern void* DeeployNetwork_outputs[1];

void RunNetwork();
void InitNetwork();

typedef struct {;} _Add_tiling_closure_args_t;

#define OUTPUTTYPE int32_t
#define ISOUTPUTFLOAT 0

#endif  //  _ADDER_UNTILED_HEADER_
