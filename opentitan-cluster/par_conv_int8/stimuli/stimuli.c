#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <math.h>
#include "pulp.h"
#include "config.h"
#include "data.h"
#include "pmsis.h"
// #include "pulp_nn_utils.h"
// #include "pulp_nn_kernels.h"
#include "implem.h"
#include "pos/implem/perf.h"
#include "utils.h"

#define FPGA_EMULATION

#ifdef FPGA_EMULATION
  int baud_rate = 9600;
  int test_freq = 25000000;
#else
  int baud_rate = 115200;
  int test_freq = 100000000;
#endif

#define STACK_SIZE 2048

// Allocate buffers aligned
DATA_LOCATION int8_t matA[DIM_IN_X*DIM_IN_Y] __attribute__((aligned(4)));
DATA_LOCATION int8_t matC[DIM_OUT_X*DIM_OUT_Y*CH_OUT] __attribute__((aligned(4))) = {0};
// im2col buffer: needs size 2 * NUM_CORES * CH_IN * KERN_X * KERN_Y
#define IM2COL_SIZE (2 * NUM_CORES * CH_IN * KERN_X * KERN_Y)
DATA_LOCATION int8_t im2col_buf[IM2COL_SIZE] __attribute__((aligned(4)));

#ifdef BATCH_NORM
DATA_LOCATION int32_t kappa[CH_OUT] __attribute__((aligned(4)));
DATA_LOCATION int32_t lambda[CH_OUT] __attribute__((aligned(4)));
#endif

int retval = -1;

void matrix_init(void) {
  // load input data from generated arrays
  //  for (int i = 0; i < DIM_IN_X*DIM_IN_Y; i++) {
  //  matA[i] = pIn[i];
    //    matB[i] = B_mat[i];
  // }
  // clear output
  for (int i = 0; i < DIM_OUT_X * DIM_OUT_Y * CH_OUT; i++) {
    matC[i] = 0;
  }
#ifdef BIAS
  for (int i = 0; i < CH_OUT; i++) {
    // if bias array exists
    // pBias from data.h named pBias
    kappa[i] = /* load kappa[i] if available */ 0;
    lambda[i] = /* load lambda[i] if available */ 0;
  }
#endif
}

int check_result(int8_t *result) {
  int err = 0;
  for (int i = 0; i < DIM_OUT_X * DIM_OUT_Y * CH_OUT; i++) {
    int diff = abs((int8_t)result[i] - (int8_t)ref[i]);
    printf_cl("At index %d: read %d (ref: %d)\r\n",i,(int8_t)result[i],(int8_t)ref[i]);
    if (diff > 0){
      err++;
    }
  }
  return err;
}

void main_fn(int *retval) {
  if (core_id() == 0) {
    matrix_init();
    uart_set_cfg_cl(0,(test_freq/baud_rate)>>4);
  }
  synch_barrier();

  if (core_id() == 0) {
     pi_perf_conf(1<<PI_PERF_CYCLES | 1<<PI_PERF_INSTR);
     pi_perf_reset();
     pi_perf_start();
     printf_cl("[CORE %d]: Start perf counter\r\n",core_id());
  }

  // call the convolution kernel
  pulp_nn_conv_i8_i8_i8(
    pIn,             // pIn
    im2col_buf,       // pIm2ColBuffer
    pBias,            // pBias from data.h
    matC,             // pOut
    pWeight,          // pWeight from data.h
#ifdef BATCH_NORM
    kappa,            // pKappa
    lambda,           // pLambda
#else
    NULL,             // pKappa
    NULL,             // pLambda
#endif
    OUT_MULT,         // out_mult
    OUT_SHIFT,        // out_shift
    DIM_IN_X,         // dim_in_x
    DIM_IN_Y,         // dim_in_y
    CH_IN,            // ch_in
    DIM_OUT_X,        // dim_out_x
    DIM_OUT_Y,        // dim_out_y
    CH_OUT,           // ch_out
    KERN_X,           // dim_kernel_x
    KERN_Y,           // dim_kernel_y
    PAD_TOP,          // padding_y_top
    PAD_BOTTOM,       // padding_y_bottom
    PAD_LEFT,         // padding_x_left
    PAD_RIGHT,        // padding_x_right
    STRIDE_X,         // stride_x
    STRIDE_Y,         // stride_y
    FLAG_RELU,        // flag_relu
    FLAG_BATCH_NORM   // flag_batch_norm
  );

  if (core_id() == 0) {
     pi_perf_stop();
     uint32_t instr_cnt = pi_perf_read(PI_PERF_INSTR);
     uint32_t cycles_cnt = pi_perf_read(PI_PERF_CYCLES);
     printf("Num.Istr: %d - Num.Cycles: %d\r\n",instr_cnt,cycles_cnt);
  }

#ifdef CHECK
  if (core_id() == 0) {
    *retval = check_result(matC);
  }
#endif
}

int main() {
  synch_barrier();
  main_fn(&retval);
  synch_barrier();
  if (core_id() == 0) {
    pulp_write32(0x10404008, retval);
    pulp_write32(0x10404020, 0x1);
  }
  return 0;
}
