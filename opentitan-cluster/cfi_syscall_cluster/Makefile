# Copyright 2026 Fondazione Chips-IT.
# Licensed under the Apache License, Version 2.0, see LICENSE for details.
# SPDX-License-Identifier: Apache-2.0

PULP_APP = test

PULP_APP_FC_SRCS = test.c

# Shared protocol + register headers live in the Ibex common tree.
# Resolve it relative to this Makefile so the include works from any CWD.
#   sw/tests/regression_tests/opentitan-cluster/<test>/  ->  sw/tests/common
CFI_COMMON_DIR := $(abspath $(dir $(lastword $(MAKEFILE_LIST)))../../../common)

PULP_CFLAGS += -O3 -g3
PULP_CFLAGS += -mno-memcpy
PULP_CFLAGS += -DNO_STANDALONE
PULP_CFLAGS += -I$(CFI_COMMON_DIR)

XLEN := 32

include $(PULP_SDK_HOME)/install/rules/pulp_rt.mk
