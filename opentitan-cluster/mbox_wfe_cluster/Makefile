# Copyright (C) 2026 ETH Zurich, University of Bologna and Fondazione Chips-IT
# Licensed under the Apache License, Version 2.0

PULP_APP = test

PULP_APP_FC_SRCS = test.c

PULP_CFLAGS += -O3 -g3
PULP_CFLAGS += -mno-memcpy

# NO_STANDALONE selects the interrupt-capable mailbox register map
# (ARCHI_MAILBOX_IRQ_RCV_* and ARCHI_MAILBOX_LETTER* offsets).
PULP_CFLAGS += -DNO_STANDALONE

XLEN := 32

include $(PULP_SDK_HOME)/install/rules/pulp_rt.mk
