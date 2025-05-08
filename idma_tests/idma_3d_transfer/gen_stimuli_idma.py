#!/usr/bin/env python

import sys
import random
import argparse
import math
import re


parser = argparse.ArgumentParser(description='Generate stimuli for iDMA simple transfer')

args = parser.parse_args()

def write_transfer_parameters_struct(f, name):
    f.write ('typedef struct {\n')
    f.write ('  unsigned int nb_words;\n')
    f.write ('  unsigned int src_stride_2d;\n')
    f.write ('  unsigned int dst_stride_2d;\n')
    f.write ('  unsigned int src_stride_3d;\n')
    f.write ('  unsigned int dst_stride_3d;\n')
    f.write ('  unsigned int reps_3d;\n')
    f.write ('} %s;\n\n' % name)
    return

def write_transfer_parameters_array(f, name, arr):
    f.write ('TransferParameters %s[] = {\n' % name)
    for v in arr:
        nb_words = random.randint(1, MAX_SIZE)
        src_stride_2d = random.randint(1, MAX_STRIDE)
        dst_stride_2d = random.randint(1, MAX_STRIDE)
        src_stride_3d = random.randint(1, MAX_STRIDE)
        dst_stride_3d = random.randint(1, MAX_STRIDE)
        reps_3d       = random.randint(1, MAX_REPS)
        f.write('{%d, %d, %d, %d, %d, %d}, \n' % (nb_words, src_stride_2d, dst_stride_2d, src_stride_3d, dst_stride_3d, reps_3d))
    f.write('};\n\n')
    return

def write_define(f, name,val):
    f.write('#define %s %d\n\n' % (name,val))
    return

# Randomize between 1 and 50 the number of simple iDMA transfers to be performed
# For each transfer we set a size in bytes (the number of 32-bits elements that 
# will be moved is size / sizeof(uint32_t)).

NB_TRANSFERS = random.randint(1, 50)

# Randomize between 1 and 500 the number of words to be transferred
# on each 2D page
# Actual size of the 3D transfer is nb_words * sizeof(uint32_t) * reps_3d

MAX_SIZE     = 50
MAX_STRIDE   = 3
MAX_REPS     = 10
transfer_params = [None] * NB_TRANSFERS

f_params    = open('idma_parameters.h', 'w')
f_defines   = open('idma_defines.h', 'w')

write_define(f_defines, 'NB_TRANSFERS', NB_TRANSFERS)
write_transfer_parameters_struct(f_params, 'TransferParameters')
write_transfer_parameters_array(f_params, 'transfer_params', transfer_params)

f_params.close()
f_defines.close()