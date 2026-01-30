
#ifndef _ATTENTION_TILED_HEADER_
#define _ATTENTION_TILED_HEADER_

#include "XpulpV2/32bit/include/pulp_nn_utils.h"
#include "XpulpV2/32bit/include/pulp_nn_kernels.h"

#include "pulp.h"

#include "stdint.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#define CLAMP(x, low, high) (((x) > (high)) ? (high) : (((x) < (low)) ? (low) : (x)))

extern int8_t *DeeployNetwork_MEMORYARENA_L1;
extern uint32_t DeeployNetwork_MEMORYARENA_L1_len;
extern int8_t *DeeployNetwork_MEMORYARENA_L2;
extern uint32_t DeeployNetwork_MEMORYARENA_L2_len;
extern int8_t *DeeployNetwork_input_0;
extern uint32_t DeeployNetwork_input_0_len;
extern int8_t *DeeployNetwork_output_0;
extern uint32_t DeeployNetwork_output_0_len;
extern uint32_t DeeployNetwork_num_inputs;
extern uint32_t DeeployNetwork_num_outputs;
extern void *DeeployNetwork_inputs[1];
extern void *DeeployNetwork_outputs[1];
extern uint32_t DeeployNetwork_inputs_bytes[1];
extern uint32_t DeeployNetwork_outputs_bytes[1];

extern uint8_t DeeployNetwork_TILING_CODEGEN_L1__MERGE_GEMM_MATMUL_RQ_PASS_0_numTiles[2];
extern uint8_t DeeployNetwork_TILING_CODEGEN_L1__MERGE_GEMM_MATMUL_RQ_PASS_1_numTiles[2];
extern uint8_t DeeployNetwork_TILING_CODEGEN_L1__MERGE_GEMM_MATMUL_RQ_PASS_2_numTiles[2];
extern uint8_t DeeployNetwork_TILING_CODEGEN_L1_Transpose_32_numTiles[2];
extern uint8_t DeeployNetwork_TILING_CODEGEN_L1__MERGE_TRANSPOSES_PASS_0_numTiles[2];
extern uint8_t DeeployNetwork_TILING_CODEGEN_L1__MERGE_TRANSPOSES_PASS_1_numTiles[2];
extern uint8_t DeeployNetwork_TILING_CODEGEN_L1__MERGE_GEMM_MATMUL_RQ_PASS_3_numTiles[2];
extern uint8_t DeeployNetwork_TILING_CODEGEN_L1_iSoftmax_41_numTiles[2];
extern uint8_t DeeployNetwork_TILING_CODEGEN_L1__MERGE_GEMM_MATMUL_RQ_PASS_4_numTiles[2];
extern uint8_t DeeployNetwork_TILING_CODEGEN_L1_Transpose_44_numTiles[2];
extern uint8_t DeeployNetwork_TILING_CODEGEN_L1__MERGE_GEMM_MATMUL_RQ_PASS_5_numTiles[2];
extern uint8_t DeeployNetwork_TILING_CODEGEN_L1__MERGE_ADDRQ_PASS_0_numTiles[2];
extern uint8_t DeeployNetwork_TILING_CODEGEN_L1__MERGE_GEMM_MATMUL_RQ_PASS_6_numTiles[2];
extern uint8_t DeeployNetwork_TILING_CODEGEN_L1__MERGE_GEMM_MATMUL_RQ_PASS_7_numTiles[2];
extern uint8_t DeeployNetwork_TILING_CODEGEN_L1__MERGE_ADDRQ_PASS_1_numTiles[2];

void InitNetwork();
void RunNetwork();

extern void *testInputVector[1];
extern void *testOutputVector[1];

typedef struct {
  int8_t *DeeployNetwork_TILING_CODEGEN_L1__MERGE_GEMM_MATMUL_RQ_PASS_0_A_ref;
  int8_t *DeeployNetwork_TILING_CODEGEN_L1__MERGE_GEMM_MATMUL_RQ_PASS_0_B_ref;
  int32_t *DeeployNetwork_TILING_CODEGEN_L1__MERGE_GEMM_MATMUL_RQ_PASS_0_mul_ref;
  int32_t *DeeployNetwork_TILING_CODEGEN_L1__MERGE_GEMM_MATMUL_RQ_PASS_0_C_ref;
  int8_t *DeeployNetwork_TILING_CODEGEN_L1__MERGE_GEMM_MATMUL_RQ_PASS_0_data_out_ref;
} __MERGE_GEMM_MATMUL_RQ_PASS_0_tiling_closure_args_t;

#define OUTPUT_TYPE int8_t

#endif  //  _ATTENTION_TILED_HEADER_
