PULP_APP = test
PULP_APP_SRCS = dmr_matmul.c

PULP_CFLAGS = -O3
PULP_LDFLAGS = -lm

ifeq ($(fault_inject),1)
	export FAULT_INJECTION=1
	export FAULT_INJECTION_SCRIPT=$(CURDIR)/pulp_inject_fault.tcl
endif

ifdef DMR_NO_RAPID_RECOVERY
PULP_CFLAGS += -DDMR_NO_RAPID_RECOVERY
endif

ifdef DMR_TIMING_DIVERSITY
PULP_CFLAGS += -DDMR_TIMING_DIVERSITY
endif

include $(PULP_SDK_HOME)/install/rules/pulp.mk
