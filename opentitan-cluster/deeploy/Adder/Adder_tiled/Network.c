#include "Network.h"

int8_t *DeeployNetwork_MEMORYARENA_L1;
uint32_t DeeployNetwork_MEMORYARENA_L1_len = 750;
int8_t *DeeployNetwork_MEMORYARENA_L2;
uint32_t DeeployNetwork_MEMORYARENA_L2_len = 750;
int8_t *DeeployNetwork_input_0;
uint32_t DeeployNetwork_input_0_len = 125;
int8_t *DeeployNetwork_input_1;
uint32_t DeeployNetwork_input_1_len = 125;
int32_t *DeeployNetwork_output_0;
uint32_t DeeployNetwork_output_0_len = 125;
uint32_t DeeployNetwork_num_inputs = 2;
uint32_t DeeployNetwork_num_outputs = 1;
void *DeeployNetwork_inputs[2];
void *DeeployNetwork_outputs[1];
uint32_t DeeployNetwork_inputs_bytes[2] = {125, 125};
uint32_t DeeployNetwork_outputs_bytes[1] = {500};
uint8_t DeeployNetwork_TILING_CODEGEN_L1_Add_numTiles[2] = {0, 1};

static void _Add_tiling_closure(void *_Add_tiling_closure_args) {
  // CLOSURE ARG CAST
  _Add_tiling_closure_args_t *args = (_Add_tiling_closure_args_t *)_Add_tiling_closure_args;
  int8_t *DeeployNetwork_TILING_CODEGEN_L1_Add_data_in_1_ref = args->DeeployNetwork_TILING_CODEGEN_L1_Add_data_in_1_ref;
  int8_t *DeeployNetwork_TILING_CODEGEN_L1_Add_data_in_2_ref = args->DeeployNetwork_TILING_CODEGEN_L1_Add_data_in_2_ref;
  int32_t *DeeployNetwork_TILING_CODEGEN_L1_Add_data_out_ref = args->DeeployNetwork_TILING_CODEGEN_L1_Add_data_out_ref;

  // CLOSURE FUNCTION CALL

  // Add (Name: Add, Op: Add)
  for (uint32_t i = 0; i < 125; i++) {
    DeeployNetwork_TILING_CODEGEN_L1_Add_data_out_ref[i] =
        DeeployNetwork_TILING_CODEGEN_L1_Add_data_in_1_ref[i] + DeeployNetwork_TILING_CODEGEN_L1_Add_data_in_2_ref[i] + 0;
  }

  // CLOSURE ARG WRITEBACK
}

typedef struct {
  int8_t *DeeployNetwork_TILING_CODEGEN_L1_Add_data_in_1_ref;
  int8_t *DeeployNetwork_TILING_CODEGEN_L1_Add_data_in_2_ref;
  int32_t *DeeployNetwork_TILING_CODEGEN_L1_Add_data_out_ref;
} _Add_cluster_fork_args_t;

static void _Add_cluster_fork(void *_Add_cluster_fork_args) {
  // CLOSURE ARG CAST
  _Add_cluster_fork_args_t *args = (_Add_cluster_fork_args_t *)_Add_cluster_fork_args;
  int8_t *DeeployNetwork_TILING_CODEGEN_L1_Add_data_in_1_ref = args->DeeployNetwork_TILING_CODEGEN_L1_Add_data_in_1_ref;
  int8_t *DeeployNetwork_TILING_CODEGEN_L1_Add_data_in_2_ref = args->DeeployNetwork_TILING_CODEGEN_L1_Add_data_in_2_ref;
  int32_t *DeeployNetwork_TILING_CODEGEN_L1_Add_data_out_ref = args->DeeployNetwork_TILING_CODEGEN_L1_Add_data_out_ref;

  // CLOSURE FUNCTION CALL
  _Add_tiling_closure_args_t DeeployNetwork__Add_tiling_closure_args =
      (_Add_tiling_closure_args_t){.DeeployNetwork_TILING_CODEGEN_L1_Add_data_in_1_ref = DeeployNetwork_TILING_CODEGEN_L1_Add_data_in_1_ref,
                                   .DeeployNetwork_TILING_CODEGEN_L1_Add_data_in_2_ref = DeeployNetwork_TILING_CODEGEN_L1_Add_data_in_2_ref,
                                   .DeeployNetwork_TILING_CODEGEN_L1_Add_data_out_ref = DeeployNetwork_TILING_CODEGEN_L1_Add_data_out_ref};

  // _Add_tiling_closure CLOSURE CALL
  _Add_tiling_closure(&DeeployNetwork__Add_tiling_closure_args);

  // CLOSURE ARG WRITEBACK
}

typedef struct {
  uint8_t *DeeployNetwork_TILING_CODEGEN_L1_Add_tileIdxPtr;
} _Add_closure_args_t;

static void _Add_closure(void *_Add_closure_args) {
  // CLOSURE ARG CAST
  _Add_closure_args_t *args = (_Add_closure_args_t *)_Add_closure_args;
  uint8_t *DeeployNetwork_TILING_CODEGEN_L1_Add_tileIdxPtr = args->DeeployNetwork_TILING_CODEGEN_L1_Add_tileIdxPtr;

  // CLOSURE FUNCTION CALL
  int8_t *DeeployNetwork_TILING_CODEGEN_L1_Add_data_in_1_ref = (int8_t *)((char *)DeeployNetwork_MEMORYARENA_L1 + 500);
  int8_t *DeeployNetwork_TILING_CODEGEN_L1_Add_data_in_2_ref = (int8_t *)((char *)DeeployNetwork_MEMORYARENA_L1 + 625);
  int32_t *DeeployNetwork_TILING_CODEGEN_L1_Add_data_out_ref = (int32_t *)((char *)DeeployNetwork_MEMORYARENA_L1 + 0);
  void *DeeployNetwork_TILING_CODEGEN_L1_Add_input_0_ref = (void *)((char *)DeeployNetwork_input_0 + 0);
  void *DeeployNetwork_TILING_CODEGEN_L1_Add_input_1_ref = (void *)((char *)DeeployNetwork_input_1 + 0);
  void *DeeployNetwork_TILING_CODEGEN_L1_Add_output_0_ref = (void *)((char *)DeeployNetwork_output_0 + 0);

  // Initialize DMA futures

  // TILING LOOP
  for (int TILING_I = DeeployNetwork_TILING_CODEGEN_L1_Add_numTiles[*DeeployNetwork_TILING_CODEGEN_L1_Add_tileIdxPtr];
       TILING_I < DeeployNetwork_TILING_CODEGEN_L1_Add_numTiles[(*DeeployNetwork_TILING_CODEGEN_L1_Add_tileIdxPtr) + 1]; TILING_I++) {

    // Transfer input tiles
    plp_cl_dma_wait_toL1(pulp_cl_idma_L2ToL1((unsigned int) DeeployNetwork_TILING_CODEGEN_L1_Add_input_0_ref, (unsigned int) DeeployNetwork_TILING_CODEGEN_L1_Add_data_in_1_ref, 125));
    plp_cl_dma_wait_toL1(pulp_cl_idma_L2ToL1((unsigned int) DeeployNetwork_TILING_CODEGEN_L1_Add_input_1_ref, (unsigned int) DeeployNetwork_TILING_CODEGEN_L1_Add_data_in_2_ref, 125));

    // Wait for input tiles

    _Add_cluster_fork_args_t DeeployNetwork__Add_cluster_fork_args =
        (_Add_cluster_fork_args_t){.DeeployNetwork_TILING_CODEGEN_L1_Add_data_in_1_ref = DeeployNetwork_TILING_CODEGEN_L1_Add_data_in_1_ref,
                                   .DeeployNetwork_TILING_CODEGEN_L1_Add_data_in_2_ref = DeeployNetwork_TILING_CODEGEN_L1_Add_data_in_2_ref,
                                   .DeeployNetwork_TILING_CODEGEN_L1_Add_data_out_ref = DeeployNetwork_TILING_CODEGEN_L1_Add_data_out_ref};

    _Add_cluster_fork(&DeeployNetwork__Add_cluster_fork_args);
    // Transfer output tiles

    plp_cl_dma_wait_toL1(pulp_cl_idma_L1ToL2((unsigned int) DeeployNetwork_TILING_CODEGEN_L1_Add_data_out_ref, (unsigned int) DeeployNetwork_TILING_CODEGEN_L1_Add_output_0_ref, 500));

    // Wait for output tiles

    // CLOSE TILING LOOP
  }
  *DeeployNetwork_TILING_CODEGEN_L1_Add_tileIdxPtr += 1;

  // Deinitialize DMA futures

  // CLOSURE ARG WRITEBACK
}

typedef struct {
  uint8_t *DeeployNetwork_TILING_CODEGEN_L1_Add_tileIdxPtr;
} _Add_closure_L3_args_t;

static void _Add_closure_L3(void *_Add_closure_L3_args) {
  // CLOSURE ARG CAST
  _Add_closure_L3_args_t *args = (_Add_closure_L3_args_t *)_Add_closure_L3_args;
  uint8_t *DeeployNetwork_TILING_CODEGEN_L1_Add_tileIdxPtr = args->DeeployNetwork_TILING_CODEGEN_L1_Add_tileIdxPtr;

  // CLOSURE FUNCTION CALL
  _Add_closure_args_t DeeployNetwork__Add_closure_args =
      (_Add_closure_args_t){.DeeployNetwork_TILING_CODEGEN_L1_Add_tileIdxPtr = DeeployNetwork_TILING_CODEGEN_L1_Add_tileIdxPtr};

  // _Add_closure CLOSURE CALL
  _Add_closure(&DeeployNetwork__Add_closure_args);

  // CLOSURE ARG WRITEBACK
}

void RunNetwork() {

  uint8_t bu_DeeployNetwork_TILING_CODEGEN_L1_Add_tileIdxPtr = 0;
  uint8_t *DeeployNetwork_TILING_CODEGEN_L1_Add_tileIdxPtr = &bu_DeeployNetwork_TILING_CODEGEN_L1_Add_tileIdxPtr;
  _Add_closure_L3_args_t DeeployNetwork__Add_closure_L3_args =
      (_Add_closure_L3_args_t){.DeeployNetwork_TILING_CODEGEN_L1_Add_tileIdxPtr = DeeployNetwork_TILING_CODEGEN_L1_Add_tileIdxPtr};

  // _Add_closure_L3 CLOSURE CALL
  _Add_closure_L3(&DeeployNetwork__Add_closure_L3_args);
}

void InitNetwork() {

  DeeployNetwork_MEMORYARENA_L1 = (int8_t *)pi_l1_malloc(0, sizeof(int8_t) * DeeployNetwork_MEMORYARENA_L1_len);

  DeeployNetwork_MEMORYARENA_L2 = (int8_t *)pi_l2_malloc(sizeof(int8_t) * DeeployNetwork_MEMORYARENA_L2_len);

  DeeployNetwork_input_0 = (int8_t *)((char *)DeeployNetwork_MEMORYARENA_L2 + 625);
  DeeployNetwork_input_1 = (int8_t *)((char *)DeeployNetwork_MEMORYARENA_L2 + 500);
  DeeployNetwork_output_0 = (int32_t *)((char *)DeeployNetwork_MEMORYARENA_L2 + 0);
  DeeployNetwork_inputs[0] = (void *)DeeployNetwork_input_0;
  DeeployNetwork_inputs[1] = (void *)DeeployNetwork_input_1;
  DeeployNetwork_outputs[0] = (void *)DeeployNetwork_output_0;
}
