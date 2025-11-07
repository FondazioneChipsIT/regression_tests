PULP_APP = stimuli

PULP_APP_FC_SRCS = stimuli.c typedefs.h
PULP_APP_SRCS =  conv.c support_func.c

PULP_APP_SRCS +=  pulp-nn-mixed/XpulpV2/32bit/src/Add/*.c
PULP_APP_SRCS +=  pulp-nn-mixed/XpulpV2/32bit/src/Convolution/pulp_nn_conv_i8_i8_i8.c
PULP_APP_SRCS +=  pulp-nn-mixed/XpulpV2/32bit/src/MatrixMultiplication/pulp_nn_matmul_i8_i8_i8.c
PULP_APP_SRCS +=  $(PULP_NN)/../cluster/common/utils.c

PULP_CFLAGS += -O3 -g3
PULP_CFLAGS += -mno-memcpy

XLEN:=32

PULP_CFLAGS += -DNUM_CORES=8 -DUSE_INTRINSICS  -DINT8 -DCHECK

PULP_CFLAGS += -fno-tree-vectorize

ifdef print_results
PULP_CFLAGS += -DPRINT_RESULTS
endif

ifdef verbose2
PULP_CFLAGS += -DVERBOSE
endif

ifdef stats
PULP_CFLAGS += -DSTATS
endif


gen_args:
	python3 gen_data.py  --dim_in_x 12 --dim_in_y 1 --ch_in 1 --ch_out 2 --kern_x 3 --kern_y 1 --stride_y 1 --stride_x 1 --pad_top 0 --pad_bottom 0 --pad_left 1 --pad_right 1 --out_mult 1 --out_shift 0 -o data.h

include $(PULP_SDK_HOME)/install/rules/pulp_rt.mk

