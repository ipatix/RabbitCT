# Supported: GCC, CLANG, ICX, NVCC, HIP
TOOLCHAIN ?= CLANG
ENABLE_OPENMP ?= false
ENABLE_LIKWID ?= false
ENABLE_ISPC ?= false
# Supported: SSE, AVX, AVX512, NEON
SIMD ?= SSE

# GPU architecture (only used when TOOLCHAIN=NVCC or HIP)
# # NVCC:
#       -gencode=arch=compute_80,code=sm_80 # for A100
#       -gencode=arch=compute_86,code=sm_86 # for A40
#       -gencode=arch=compute_90,code=sm_90 # for GH200
CUDA_ARCH ?= -gencode=arch=compute_80,code=sm_80 -gencode=arch=compute_86,code=sm_86 -gencode=arch=compute_90,code=sm_90
# # HIP:
#       gfx908 # for MI100
#       gfx90a # for MI210A
#       gfx1030 # for RX 6900 XT
#       gfx942 # for MI300X & MI300A
HIP_ARCH  ?= gfx1030,gfx942

# Whether to enable the texture fetching based CUDA kernel.
# Not supported on some AMD GPUs (via HIP)
ENABLE_CUDA_TEX ?= true

#Feature options
OPTIONS +=  -DARRAY_ALIGNMENT=64
OPTIONS +=  -DMAX_NUM_THREADS=128
#OPTIONS +=  -DVERBOSE_AFFINITY
#OPTIONS +=  -DVERBOSE_DATASIZE
#OPTIONS +=  -DVERBOSE_TIMER

################################################################
# DO NOT EDIT BELOW !!!
################################################################
DEFINES =
DEFINES += -DSIMD_NAME=\"$(SIMD)\"

ifeq ($(SIMD), SSE)
DEFINES +=  -DVECTORSIZE=4
endif
ifeq ($(SIMD), AVX)
DEFINES +=  -DVECTORSIZE=8
endif
ifeq ($(SIMD), AVX512)
DEFINES +=  -DVECTORSIZE=16
endif
ifeq ($(SIMD), NEON)
DEFINES +=  -DVECTORSIZE=4
endif

# SSE/AVX/AVX512 require x86-64; NEON requires AARCH64.
ARCH := $(shell uname -m)
ifeq ($(SIMD),SSE)
ifneq ($(ARCH),x86_64)
$(error SIMD=SSE requires x86-64 but detected $(ARCH))
endif
endif
ifeq ($(SIMD),AVX)
ifneq ($(ARCH),x86_64)
$(error SIMD=AVX requires x86-64 but detected $(ARCH))
endif
endif
ifeq ($(SIMD),AVX512)
ifneq ($(ARCH),x86_64)
$(error SIMD=AVX512 requires x86-64 but detected $(ARCH))
endif
endif
ifeq ($(SIMD),NEON)
ifneq ($(filter $(ARCH),arm64 aarch64),$(ARCH))
$(error SIMD=NEON requires AARCH64 but detected $(ARCH))
endif
endif
