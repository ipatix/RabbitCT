# Copyright (C) NHR@FAU, University Erlangen-Nuremberg.
# All rights reserved. This file is part of RabbitCT.
# Use of this source code is governed by a MIT-style
# license that can be found in the LICENSE file.

#CONFIGURE BUILD SYSTEM
TARGET	   = rabbitRunner-$(TOOLCHAIN)
BUILD_DIR  = ./build/$(TOOLCHAIN)
SRC_DIR    = ./src
MAKE_DIR   = ./mk
Q         ?= @

#DO NOT EDIT BELOW
ifeq (,$(wildcard config.mk))
$(info )
$(info ====================================================================)
$(info config.mk does not exist!)
$(info Creating config.mk from ./mk/config-default.mk)
$(info Please adapt config.mk to your needs and run make again.)
$(info ====================================================================)
$(info )
$(shell cp ./mk/config-default.mk config.mk)
$(error Stopping after creating config.mk - please review and run make again)
endif
include config.mk
include $(MAKE_DIR)/include_$(TOOLCHAIN).mk
include $(MAKE_DIR)/include_LIKWID.mk
INCLUDES  += -I$(SRC_DIR)/includes -I$(SRC_DIR) -I$(BUILD_DIR)

VPATH     = $(SRC_DIR)
ASM       = $(patsubst $(SRC_DIR)/%.c, $(BUILD_DIR)/%.s,$(wildcard $(SRC_DIR)/*.c))
OBJ       = $(patsubst $(SRC_DIR)/%.c, $(BUILD_DIR)/%.o,$(wildcard $(SRC_DIR)/*.c))

# ISPC support: compile fastRabbit.ispc and include LolaISPC variant
ifeq ($(ENABLE_ISPC),true)
ISPC      ?= ispc
ISPCFLAGS  = --pic --opt=fast-math
ifeq ($(SIMD),SSE)
ISPCFLAGS += --target=sse4-i32x4
endif
ifeq ($(SIMD),AVX)
ISPCFLAGS += --target=avx2-i32x8
endif
ifeq ($(SIMD),AVX512)
ISPCFLAGS += --target=avx512skx-i32x16
endif
OBJ       += $(BUILD_DIR)/fastRabbit.o
DEFINES   += -DENABLE_ISPC
else
# Exclude LolaISPC.c when ISPC is disabled (missing symbols)
OBJ       := $(filter-out $(BUILD_DIR)/LolaISPC.o,$(OBJ))
endif

ifeq ($(ENABLE_VULKAN),true)
DEFINES   += -DENABLE_VULKAN
LIBS      += -lvulkan
OBJ       += $(patsubst $(SRC_DIR)/%.frag, $(BUILD_DIR)/%.frag.o,$(wildcard $(SRC_DIR)/*.frag))
OBJ       += $(patsubst $(SRC_DIR)/%.vert, $(BUILD_DIR)/%.vert.o,$(wildcard $(SRC_DIR)/*.vert))
else
OBJ       := $(filter-out $(BUILD_DIR)/LolaVK.o,$(OBJ))
endif

ifeq ($(ENABLE_CUDA),true)
OBJ       += $(patsubst $(SRC_DIR)/%.cu, $(BUILD_DIR)/%.o,$(wildcard $(SRC_DIR)/*.cu))
endif

# Select assembly kernel matching the SIMD option.
# SSE/AVX/AVX512 require x86-64; NEON requires AARCH64.
ARCH := $(shell uname -m)
ifeq ($(SIMD),SSE)
ifneq ($(ARCH),x86_64)
$(error SIMD=SSE requires x86-64 but detected $(ARCH))
endif
OBJ       += $(patsubst $(SRC_DIR)/%.S, $(BUILD_DIR)/%.o,$(SRC_DIR)/fastRabbitSSE.S)
endif
ifeq ($(SIMD),AVX)
ifneq ($(ARCH),x86_64)
$(error SIMD=AVX requires x86-64 but detected $(ARCH))
endif
OBJ       += $(patsubst $(SRC_DIR)/%.S, $(BUILD_DIR)/%.o,$(SRC_DIR)/fastRabbitAVX.S)
endif
ifeq ($(SIMD),AVX512)
ifneq ($(ARCH),x86_64)
$(error SIMD=AVX512 requires x86-64 but detected $(ARCH))
endif
OBJ       += $(patsubst $(SRC_DIR)/%.S, $(BUILD_DIR)/%.o,$(SRC_DIR)/fastRabbitAVX512.S)
endif
ifeq ($(SIMD),NEON)
ifneq ($(filter $(ARCH),arm64 aarch64),$(ARCH))
$(error SIMD=NEON requires AARCH64 but detected $(ARCH))
endif
OBJ       += $(patsubst $(SRC_DIR)/%.S, $(BUILD_DIR)/%.o,$(SRC_DIR)/fastRabbitNEON.S)
endif
SRC       =  $(wildcard $(SRC_DIR)/*.h $(SRC_DIR)/*.c)
CPPFLAGS := $(CPPFLAGS) $(DEFINES) $(OPTIONS) $(INCLUDES)
c := ,
clist = $(subst $(eval) ,$c,$(strip $1))

define CLANGD_TEMPLATE
CompileFlags:
  Add: [$(call clist,$(CPPFLAGS)), $(call clist,$(CFLAGS)), -xc]
  Compiler: clang
endef

${TARGET}: $(BUILD_DIR) .clangd $(OBJ) $(DATA_DIR)
	$(info ===>  LINKING  $(TARGET))
	$(Q)${LD} ${LFLAGS} -o $(TARGET) $(OBJ) $(LIBS)

$(BUILD_DIR)/%.o:  %.c $(MAKE_DIR)/include_$(TOOLCHAIN).mk config.mk
	$(info ===>  COMPILE  $@)
	$(CC) -c $(CPPFLAGS) $(CFLAGS) $< -o $@
	$(Q)$(CC) $(CPPFLAGS) -MT $(@:.d=.o) -MM  $< > $(BUILD_DIR)/$*.d

$(BUILD_DIR)/%.o:  %.cu $(MAKE_DIR)/include_$(TOOLCHAIN).mk config.mk
	$(info ===>  COMPILE  $@)
	$(CC) -c $(CPPFLAGS) $(CFLAGS) $< -o $@
	$(Q)$(CC) $(CPPFLAGS) -MT $(@:.d=.o) -MM  $< > $(BUILD_DIR)/$*.d

$(BUILD_DIR)/%.s:  %.c
	$(info ===>  GENERATE ASM  $@)
	$(CC) -S $(CPPFLAGS) $(CFLAGS) $< -o $@

$(BUILD_DIR)/%.o:  %.S $(MAKE_DIR)/include_$(TOOLCHAIN).mk config.mk
	$(info ===>  ASSEMBLE  $@)
	$(Q)$(CC) -c $(CPPFLAGS) $< -o $@

# ISPC compilation: .ispc -> .o + generated header
ifeq ($(ENABLE_ISPC),true)
$(BUILD_DIR)/fastRabbit.o $(BUILD_DIR)/fastRabbit_ispc.h: $(SRC_DIR)/fastRabbit.ispc $(MAKE_DIR)/include_$(TOOLCHAIN).mk config.mk

	$(info ===>  ISPC  $<)
	$(ISPC) $(ISPCFLAGS) $< -o $(BUILD_DIR)/fastRabbit.o -h $(BUILD_DIR)/fastRabbit_ispc.h

$(BUILD_DIR)/fastRabbit.s: $(SRC_DIR)/fastRabbit.ispc
	$(info ===>  ISPC ASM  $<)
	$(Q)$(ISPC) $(ISPCFLAGS) --emit-asm $< -o $@

$(BUILD_DIR)/LolaISPC.o: $(BUILD_DIR)/fastRabbit_ispc.h
$(BUILD_DIR)/LolaISPC.s: $(BUILD_DIR)/fastRabbit_ispc.h
ASM += $(BUILD_DIR)/fastRabbit.s
endif

ifeq ($(ENABLE_VULKAN),true)
$(BUILD_DIR)/%.frag.o: $(BUILD_DIR)/%.frag.c
	$(info ===>  COMPILE  $@)
	$(Q)$(CC) -c $(CPPFLAGS) $(CFLAGS) $< -o $@

$(BUILD_DIR)/%.vert.o: $(BUILD_DIR)/%.vert.c
	$(info ===>  COMPILE  $@)
	$(Q)$(CC) -c $(CPPFLAGS) $(CFLAGS) $< -o $@

$(BUILD_DIR)/%.frag.spv: $(SRC_DIR)/%.frag
	$(info ===>  GLSLC  $<)
	$(Q)glslc -fshader-stage=frag -o $@ $<

$(BUILD_DIR)/%.vert.spv: $(SRC_DIR)/%.vert
	$(info ===>  GLSLC  $<)
	$(Q)glslc -fshader-stage=vert -o $@ $<

$(BUILD_DIR)/%.frag.c: $(BUILD_DIR)/%.frag.spv
	$(info ===>  XXD $<)
	$(Q)xxd -n $(notdir $<) -i $< > $@

$(BUILD_DIR)/%.vert.c: $(BUILD_DIR)/%.vert.spv
	$(info ===>  XXD $<)
	$(Q)xxd -n $(notdir $<) -i $< > $@
endif

.PHONY: clean distclean info asm format

clean:
	$(info ===>  CLEAN)
	@rm -rf $(BUILD_DIR)

distclean:
	$(info ===>  DIST CLEAN)
	@rm -rf build
	@rm -f rabbitRunner-*
	@rm -f .clangd compile_commands.json

info:
	$(info $(CFLAGS))
	$(Q)$(CC) $(VERSION)

asm:  $(BUILD_DIR) $(ASM)

format:
	@for src in $(SRC) ; do \
		echo "Formatting $$src" ; \
		clang-format -i $$src ; \
	done
	@echo "Done"

$(BUILD_DIR):
	@mkdir -p $(BUILD_DIR)

.clangd:
	$(file > .clangd,$(CLANGD_TEMPLATE))

-include $(OBJ:.o=.d)
