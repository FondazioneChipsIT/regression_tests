#include <stdio.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <limits.h> /* for CHAR_BIT */
#include <math.h>

#include "config.h"
//#include "pulp.h"
//#include "pmsis.h"
#include "pulp_nn_kernels.h"
#include "pulp_nn_utils.h"

void pulp_conv_int8_int8_int8(const int8_t *__restrict__ in1,
                             const int8_t *__restrict__ in2,
                             int8_t *__restrict__       out,
                             int                      size)
{
  pulp_nn_conv_i8_i8_i8(in1,in2,out,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0);
}
