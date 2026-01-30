#ifndef _TITANCFI_TILED_HEADER_
#define _TITANCFI_TILED_HEADER_

#include "XpulpV2/32bit/include/pulp_nn_utils.h"
#include "XpulpV2/32bit/include/pulp_nn_kernels.h"

#include "pulp.h"

#include "stdint.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

extern const uint32_t DeeployNetwork_num_inputs;
extern const uint32_t DeeployNetwork_num_outputs;

extern const uint32_t DeeployNetwork_inputs_bytes[1];
extern const uint32_t DeeployNetwork_outputs_bytes[1];

extern PI_L2 int32_t DeeployNetwork_qmatmulrqshiftadd_tensor[2];

extern PI_L2 int32_t DeeployNetwork_qmatmulrqshiftmul_tensor[2];

extern /*PI_L1*/ uint8_t DeeployNetwork_TILING_CODEGEN_L1__MERGE_CONVRQ_PASS_0_unsqueezeout_tensor_transpose_numTiles[2];

extern /*PI_L1*/ uint8_t DeeployNetwork_TILING_CODEGEN_L1__MERGE_CONVRQ_PASS_0_numTiles[2];

extern /*PI_L1*/ uint8_t DeeployNetwork_TILING_CODEGEN_L1__MERGE_CONVRQ_PASS_1_numTiles[2];

extern /*PI_L1*/ uint8_t DeeployNetwork_TILING_CODEGEN_L1__MERGE_CONVRQ_PASS_2_numTiles[2];

extern /*PI_L1*/ uint8_t DeeployNetwork_TILING_CODEGEN_L1_maxpool_numTiles[2];

extern /*PI_L1*/ uint8_t DeeployNetwork_TILING_CODEGEN_L1_maxpool_maxpoolout_tensor_pre_transpose_numTiles[2];

extern /*PI_L1*/ uint8_t DeeployNetwork_TILING_CODEGEN_L1__MERGE_GEMM_MATMUL_RQ_PASS_0_numTiles[2];

void *DeeployNetwork_inputs[1];
void *DeeployNetwork_outputs[1];

void *testInputVector[1];
void *testOutputVector[1];

#define OUTPUT_TYPE uint8_t
typedef struct {
  uint8_t *DeeployNetwork_TILING_CODEGEN_L1__MERGE_CONVRQ_PASS_0_unsqueezeout_tensor_transpose_data_in_ref;
  uint8_t *DeeployNetwork_TILING_CODEGEN_L1__MERGE_CONVRQ_PASS_0_unsqueezeout_tensor_transpose_data_out_ref;
} __MERGE_CONVRQ_PASS_0_unsqueezeout_tensor_transpose_tiling_closure_args_t;

// Define it here so that pulp-nn-mixed does not complain

#endif  //  _TITANCFI_HEADER_
