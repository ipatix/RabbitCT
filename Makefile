# Copyright (C) NHR@FAU, University Erlangen-Nuremberg.
# All rights reserved. This file is part of RabbitCT.
# Use of this source code is governed by a MIT-style
# license that can be found in the LICENSE file.

#CONFIGURE BUILD SYSTEM
TARGET	   = rabbitRunner-$(SIMD)-$(TOOLCHAIN)
BUILD_DIR  = ./build/$(SIMD)-$(TOOLCHAIN)
SRC_DIR    = ./src
MAKE_DIR   = ./mk
Q         ?= @

#DO NOT EDIT BELOW
ifeq ($(shell printf '%s\n' 4.4 "$(MAKE_VERSION)" | sort -V | head -1),4.4)
MAKE_GE_44 := true
else
MAKE_GE_44 := false
endif
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
ifeq ($(ENABLE_ISPC),true)
ifneq ($(MAKE_GE_44),true)
$(info NOTE: ENABLE_ISPC requires GNU make > 4.3 (found $(MAKE_VERSION)). Disabling ISPC support.)
override ENABLE_ISPC := false
endif
endif
include $(MAKE_DIR)/include_$(TOOLCHAIN).mk
include $(MAKE_DIR)/include_LIKWID.mk
INCLUDES  += -I$(SRC_DIR)/includes -I$(SRC_DIR) -I$(BUILD_DIR)

VPATH     = $(SRC_DIR)
ASM       = $(patsubst $(SRC_DIR)/%.c, $(BUILD_DIR)/%.s,$(wildcard $(SRC_DIR)/*.c))
ifeq ($(ENABLE_ISPC),true)
ASM       += $(patsubst $(SRC_DIR)/%.ispc, $(BUILD_DIR)/%.s,$(wildcard $(SRC_DIR)/*.ispc))
endif
OBJ       = $(patsubst $(SRC_DIR)/%.c, $(BUILD_DIR)/%.o,$(wildcard $(SRC_DIR)/*.c))
OBJ       += $(patsubst $(SRC_DIR)/%.S, $(BUILD_DIR)/%.o,$(SRC_DIR)/fastRabbit$(SIMD).S)
ifeq ($(ENABLE_ISPC),true)
OBJ       += $(patsubst $(SRC_DIR)/%.ispc, $(BUILD_DIR)/%.o,$(wildcard $(SRC_DIR)/*.ispc))
endif
ifeq ($(ENABLE_CUDA),true)
OBJ       += $(patsubst $(SRC_DIR)/%.cu, $(BUILD_DIR)/%.o,$(wildcard $(SRC_DIR)/*.cu))
ifneq ($(ENABLE_CUDA_TEX),true)
OBJ       := $(filter-out $(BUILD_DIR)/LolaCUDATex.o,$(OBJ))
endif
endif
include $(MAKE_DIR)/include_ISPC.mk
SRC       =  $(wildcard $(SRC_DIR)/*.h $(SRC_DIR)/*.c)
CPPFLAGS := $(CPPFLAGS) $(DEFINES) $(OPTIONS) $(INCLUDES)

ifneq (,$(filter $(TOOLCHAIN),NVCC HIP))
  CPPFLAGS += -D_GPU
  OBJ   += $(patsubst $(SRC_DIR)/%.cu, $(BUILD_DIR)/%.o,$(wildcard $(SRC_DIR)/*.cu))
endif

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

$(BUILD_DIR)/%.o: %.cu $(MAKE_DIR)/include_$(TOOLCHAIN).mk config.mk
	$(info ===>  COMPILE  $@)
	$(CC) -c $(CPPFLAGS) $(CFLAGS) $(NVCCFLAGS) $< -o $@
	$(Q)$(CC) $(CPPFLAGS) -MT $(@:.d=.o) -MM  $< > $(BUILD_DIR)/$*.d

$(BUILD_DIR)/%.s:  %.c
	$(info ===>  GENERATE ASM  $@)
	$(CC) -S $(CPPFLAGS) $(CFLAGS) $< -o $@

$(BUILD_DIR)/%.o:  %.S $(MAKE_DIR)/include_$(TOOLCHAIN).mk config.mk
	$(info ===>  ASSEMBLE  $@)
	$(CC) -c $(CPPFLAGS) $< -o $@

ifeq ($(ENABLE_ISPC),true)
$(BUILD_DIR)/%.o $(BUILD_DIR)/%_ispc.h &: %.ispc $(MAKE_DIR)/include_$(TOOLCHAIN).mk config.mk
	$(info ===>  ISPC  $<)
	$(ISPC) $(ISPCFLAGS) $< -o $(BUILD_DIR)/$*.o -h $(BUILD_DIR)/$*_ispc.h

$(BUILD_DIR)/%.s: %.ispc
	$(info ===>  ISPC ASM  $<)
	$(ISPC) $(ISPCFLAGS) --emit-asm $< -o $@
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

ifeq ($(ENABLE_ISPC),true)
$(BUILD_DIR)/LolaISPC.o: $(BUILD_DIR)/fastRabbit_ispc.h
endif

-include $(OBJ:.o=.d)
