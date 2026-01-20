
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include "pulp.h"
#include "stdint.h"
#include "testinputs.h"
#include "testoutputs.h"
// #include "pulp_nn_kernels.h"
// #include "DeeployBasicMath.h"
// #include "DeeployPULPMath.h"
// #include "bsp/ram.h"
// #include "pulp_core.h"
void RunNetwork();
void InitNetwork();

int8_t *DeeployNetwork_input_0;
int8_t *DeeployNetwork_input_1;
int32_t *DeeployNetwork_output_0;
static const uint32_t DeeployNetwork_num_inputs = 2;
static const uint32_t DeeployNetwork_num_outputs = 1;
extern void* DeeployNetwork_inputs[2];
extern void* DeeployNetwork_outputs[1];
static const uint32_t DeeployNetwork_inputs_bytes[2] = {125, 125};
static const uint32_t DeeployNetwork_outputs_bytes[1] = {500};

void* DeeployNetwork_inputs[2];void* DeeployNetwork_outputs[1];extern struct pi_device cluster_dev;
typedef struct {;} _Add_tiling_closure_args_t;

static void _Add_tiling_closure(void* _Add_tiling_closure_args){
    // CLOSURE ARG CAST
    _Add_tiling_closure_args_t* args = (_Add_tiling_closure_args_t*) _Add_tiling_closure_args;

    // CLOSURE FUNCTION CALL

    // Add (Name: Add, Op: Add)
    if (rt_core_id()==0) {
        for (uint32_t i=0;i<125;i++){
            DeeployNetwork_output_0[i] = DeeployNetwork_input_0[i] + DeeployNetwork_input_1[i] + 0;
        }
    }

    // CLOSURE ARG WRITEBACK
}

typedef struct {;} _Add_cluster_fork_args_t;

static void _Add_cluster_fork(void* _Add_cluster_fork_args){
    // CLOSURE ARG CAST
    _Add_cluster_fork_args_t* args = (_Add_cluster_fork_args_t*) _Add_cluster_fork_args;

    // CLOSURE FUNCTION CALL
    _Add_tiling_closure_args_t DeeployNetwork__Add_tiling_closure_args = (_Add_tiling_closure_args_t) {};

    // _Add_tiling_closure CLOSURE CALL
    _Add_tiling_closure(&DeeployNetwork__Add_tiling_closure_args);
    // CLOSURE ARG WRITEBACK
}

typedef struct {;} _Add_closure_args_t;

static void _Add_closure(void* _Add_closure_args){
    // CLOSURE ARG CAST
    _Add_closure_args_t* args = (_Add_closure_args_t*) _Add_closure_args;

    // CLOSURE FUNCTION CALL
    _Add_cluster_fork_args_t DeeployNetwork__Add_cluster_fork_args = (_Add_cluster_fork_args_t) {};

    // pi_cl_team_fork(8, (void*)_Add_cluster_fork, &DeeployNetwork__Add_cluster_fork_args); --> Already executing on cluster cores

    _Add_cluster_fork(&DeeployNetwork__Add_cluster_fork_args);

    // CLOSURE ARG WRITEBACK
}

typedef struct {;} _Add_closure_L3_args_t;

static void _Add_closure_L3(void* _Add_closure_L3_args){
    // CLOSURE ARG CAST
    _Add_closure_L3_args_t* args = (_Add_closure_L3_args_t*) _Add_closure_L3_args;

    // CLOSURE FUNCTION CALL
    _Add_closure_args_t DeeployNetwork__Add_closure_args = (_Add_closure_args_t) {};

    // _Add_closure CLOSURE CALL
    _Add_closure(&DeeployNetwork__Add_closure_args);

    // CLOSURE ARG WRITEBACK
}

void RunNetwork(){
    _Add_closure_L3_args_t DeeployNetwork__Add_closure_L3_args = (_Add_closure_L3_args_t) {};
    // _Add_closure_L3 CLOSURE CALL
    _Add_closure_L3(&DeeployNetwork__Add_closure_L3_args);
}

void InitNetwork(){

    DeeployNetwork_inputs[0] = (void *)DeeployNetwork_input_0;
    DeeployNetwork_inputs[1] = (void *)DeeployNetwork_input_1;
    DeeployNetwork_outputs[0] = (void *)DeeployNetwork_output_0;
}
    