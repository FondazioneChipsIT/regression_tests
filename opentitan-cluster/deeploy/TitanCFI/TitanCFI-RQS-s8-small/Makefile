PULP_APP = test

PULP_APP_FC_SRCS = test.c

PULP_CFLAGS += -g3 -O2 -fno-inline-functions -fno-inline-small-functions
PULP_CFLAGS += -mno-memcpy
PULP_CFLAGS += -I../../../../../pulp-nn-mixed/XpulpV2/32bit/include/

XLEN:=32

include $(PULP_SDK_HOME)/install/rules/pulp_rt.mk
