PULP_APP = test

PULP_APP_SRCS = Network.c testinputs.c testoutputs.c test.c

PULP_CFLAGS += -g3 -O2
PULP_CFLAGS += -I../../../../../pulp-nn-mixed/

# Flag for using the correct functions inside pulp-nn-mixed library
PULP_CFLAGS += -DUSE_RUNTIME

#Linking pulp-nn-mixed library
PULP_LDFLAGS += -L$(PULPRT_HOME)/../pulp-nn-mixed/build -lpulp-nn-mixed -Wl,--trace

XLEN:=32

include $(PULP_SDK_HOME)/install/rules/pulp.mk
