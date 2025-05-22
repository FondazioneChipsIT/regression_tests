PULP_APP = test
TEST_SRCS ?= idma_multi_core_2d.c
PULP_APP_SRCS = $(TEST_SRCS)

ifdef VERBOSE
PULP_CFLAGS += -DVERBOSE
endif

ifdef TEST_ALL_CORES
PULP_CFLAGS += -DTEST_ALL_CORES
endif

PULP_CFLAGS += -O3
stackSize = 4096

include $(PULP_SDK_HOME)/install/rules/pulp.mk

stimuli:
	python gen_stimuli_idma_2d.py