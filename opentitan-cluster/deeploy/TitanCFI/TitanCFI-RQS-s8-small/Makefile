PULP_APP = test

PULP_APP_FC_SRCS = Network.c testinputs.c testoutputs.c test.c

PULP_CFLAGS += -g3 -O2 -fno-inline-functions -fno-inline-small-functions
PULP_CFLAGS += -mno-memcpy
PULP_CFLAGS += -I../../../../../pulp-nn-mixed/

# Flag for using the correct functions inside pulp-nn-mixed library
PULP_CFLAGS += -DUSE_RUNTIME

#Linking pulp-nn-mixed library
PULP_LDFLAGS += -L$(PULPRT_HOME)/../pulp-nn-mixed -lpulp-nn-mixed -Wl,--trace

XLEN:=32

include $(PULP_SDK_HOME)/install/rules/pulp.mk
