# Copyright 2026 Fondazione Chips-IT.
# Licensed under the Apache License, Version 2.0, see LICENSE for details.
# SPDX-License-Identifier: Apache-2.0

PULP_APP = test

PULP_APP_FC_SRCS = test.c

PULP_CFLAGS += -O3 -g3
PULP_CFLAGS += -mno-memcpy
PULP_CFLAGS += -DNO_STANDALONE
# PULP_CFLAGS += -DCFI_DEBUG=1   # uncomment for per-stage debug prints

XLEN := 32

include $(PULP_SDK_HOME)/install/rules/pulp_rt.mk
