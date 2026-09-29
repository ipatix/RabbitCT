CC = nvcc
LD = $(CC)

ifeq ($(strip $(ENABLE_OPENMP)),true)
OPENMP   = -Xcompiler "-fopenmp"
endif

VERSION  = --version
CFLAGS   = -O3 $(CUDA_ARCH) $(OPENMP)
NVCCFLAGS= -std=c++17
LFLAGS   = $(OPENMP)

DEFINES  +=  -DENABLE_CUDA -D_GNU_SOURCE -DRUNTIME_BACKEND_IS_CUDA
ifeq ($(ENABLE_CUDA_TEX),true)
DEFINES  += -DENABLE_CUDA_TEX
endif
INCLUDES  =
LIBS      = -lcudart
