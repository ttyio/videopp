################################################################################
#
# Copyright 1993-2015 NVIDIA Corporation.  All rights reserved.
#
# NOTICE TO USER:
#
# This source code is subject to NVIDIA ownership rights under U.S. and
# international Copyright laws.
#
# NVIDIA MAKES NO REPRESENTATION ABOUT THE SUITABILITY OF THIS SOURCE
# CODE FOR ANY PURPOSE.  IT IS PROVIDED "AS IS" WITHOUT EXPRESS OR
# IMPLIED WARRANTY OF ANY KIND.  NVIDIA DISCLAIMS ALL WARRANTIES WITH
# REGARD TO THIS SOURCE CODE, INCLUDING ALL IMPLIED WARRANTIES OF
# MERCHANTABILITY, NONINFRINGEMENT, AND FITNESS FOR A PARTICULAR PURPOSE.
# IN NO EVENT SHALL NVIDIA BE LIABLE FOR ANY SPECIAL, INDIRECT, INCIDENTAL,
# OR CONSEQUENTIAL DAMAGES, OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS
# OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE
# OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE
# OR PERFORMANCE OF THIS SOURCE CODE.
#
# U.S. Government End Users.  This source code is a "commercial item" as
# that term is defined at 48 C.F.R. 2.101 (OCT 1995), consisting  of
# "commercial computer software" and "commercial computer software
# documentation" as such terms are used in 48 C.F.R. 12.212 (SEPT 1995)
# and is provided to the U.S. Government only as a commercial end item.
# Consistent with 48 C.F.R.12.212 and 48 C.F.R. 227.7202-1 through
# 227.7202-4 (JUNE 1995), all U.S. Government End Users acquire the
# source code with only those rights set forth herein.
#
# Location of the CUDA Toolkit
CUDA_PATH ?= "/usr/local/cuda/r75"

# architecture
HOST_ARCH   := $(shell uname -m)
TARGET_ARCH ?= $(HOST_ARCH)
ifneq (,$(filter $(TARGET_ARCH),x86_64 aarch64 ppc64le))
    TARGET_SIZE := 64
else ifeq ($(TARGET_ARCH),armv7l)
    TARGET_SIZE := 32
else
    $(error ERROR - unsupported value $(TARGET_ARCH) for TARGET_ARCH!)
endif
ifneq ($(TARGET_ARCH),$(HOST_ARCH))
    ifeq (,$(filter $(HOST_ARCH)-$(TARGET_ARCH),aarch64-armv7l x86_64-armv7l x86_64-aarch64 x86_64-ppc64le))
        $(error ERROR - cross compiling from $(HOST_ARCH) to $(TARGET_ARCH) is not supported!)
    endif
endif

# operating system
HOST_OS   := $(shell uname -s 2>/dev/null | tr "[:upper:]" "[:lower:]")
TARGET_OS ?= $(HOST_OS)
ifeq (,$(filter $(TARGET_OS),linux darwin qnx android))
    $(error ERROR - unsupported value $(TARGET_OS) for TARGET_OS!)
endif

HOST_COMPILER ?= g++
NVCC          := $(CUDA_PATH)/bin/nvcc -ccbin $(HOST_COMPILER)

# internal flags
NVCCFLAGS   := -m${TARGET_SIZE}
CCFLAGS     :=
LDFLAGS     :=

# Debug build flags
ifeq ($(dbg),1)
      NVCCFLAGS += -g -G
      BUILD_TYPE := debug
else
      BUILD_TYPE := release
endif

ALL_CCFLAGS :=
ALL_CCFLAGS += $(NVCCFLAGS)
ALL_CCFLAGS += $(EXTRA_NVCCFLAGS)
ALL_CCFLAGS += $(addprefix -Xcompiler ,$(CCFLAGS))
ALL_CCFLAGS += $(addprefix -Xcompiler ,$(EXTRA_CCFLAGS))

ALL_LDFLAGS :=
ALL_LDFLAGS += $(ALL_CCFLAGS)
ALL_LDFLAGS += $(addprefix -Xlinker ,$(LDFLAGS))
ALL_LDFLAGS += $(addprefix -Xlinker ,$(EXTRA_LDFLAGS))

# Common includes and paths for CUDA
LIBRARIES :=

################################################################################

UBUNTU = $(shell lsb_release -i -s 2>/dev/null | grep -i ubuntu)

PTX_FILE := videoPP${TARGET_SIZE}.ptx

# Gencode arguments
SMS ?=

ifeq ($(GENCODE_FLAGS),)
# Generate SASS code for each SM architecture listed in $(SMS)
$(foreach sm,$(SMS),$(eval GENCODE_FLAGS += -gencode arch=compute_$(sm),code=sm_$(sm)))

ifeq ($(SMS),)
# Generate PTX code from SM 20
GENCODE_FLAGS += -gencode arch=compute_20,code=compute_20
endif

# Generate PTX code from the highest SM architecture in $(SMS) to guarantee forward-compatibility
HIGHEST_SM := $(lastword $(sort $(SMS)))
ifneq ($(HIGHEST_SM),)
GENCODE_FLAGS += -gencode arch=compute_$(HIGHEST_SM),code=compute_$(HIGHEST_SM)
endif
endif

ifeq ($(TARGET_OS),darwin)
  ALL_LDFLAGS += -Xlinker -framework -Xlinker CUDA
else
  CUDA_SEARCH_PATH ?=
  ifneq ($(UBUNTU),)
    CUDA_SEARCH_PATH += /usr/lib
  else
    CUDA_SEARCH_PATH += /usr/lib64
  endif

  ifeq ($(TARGET_ARCH),x86_64)
    CUDA_SEARCH_PATH += $(CUDA_PATH)/lib64/stubs
  endif

  LIBRARIES += -lcuda
endif

LIBRARIES += -lcudart -lnvcuvid

ifeq ($(SAMPLE_ENABLED),0)
EXEC ?= @echo "[@]"
endif

################################################################################

# Target rules
all: build

build: videoPP $(PTX_FILE)

check.deps:
ifeq ($(SAMPLE_ENABLED),0)
	@echo "Sample will be waived due to the above missing dependencies"
else
	@echo "Sample is ready - all dependencies have been met"
endif

$(PTX_FILE): videoPP.cu
	$(EXEC) $(NVCC) $(INCLUDES) $(ALL_CCFLAGS) $(GENCODE_FLAGS) -o $@ -ptx $<
	$(EXEC) mkdir -p data
	$(EXEC) cp -f $@ ./data
	$(EXEC) mkdir -p ./bin/$(TARGET_ARCH)/$(TARGET_OS)/$(BUILD_TYPE)
	$(EXEC) cp -f $@ ./bin/$(TARGET_ARCH)/$(TARGET_OS)/$(BUILD_TYPE)

FrameQueue.o:FrameQueue.cpp
	$(EXEC) $(NVCC) $(INCLUDES) $(ALL_CCFLAGS) $(GENCODE_FLAGS) -o $@ -c $<

NvHWDecoder.o:NvHWDecoder.cpp
	$(EXEC) $(NVCC) $(INCLUDES) $(ALL_CCFLAGS) $(GENCODE_FLAGS) -o $@ -c $<

cudaProcessFrame.o:cudaProcessFrame.cpp
	$(EXEC) $(NVCC) $(INCLUDES) $(ALL_CCFLAGS) $(GENCODE_FLAGS) -o $@ -c $<

videoDecodeMain.o:videoDecodeMain.cpp
	$(EXEC) $(NVCC) $(INCLUDES) $(ALL_CCFLAGS) $(GENCODE_FLAGS) -o $@ -c $<

NvHWEncoder.o:NvHWEncoder.cpp
	$(EXEC) $(NVCC) $(INCLUDES) $(ALL_CCFLAGS) $(GENCODE_FLAGS) -o $@ -c $<
        

videoPP: NvHWEncoder.o FrameQueue.o NvHWDecoder.o cudaProcessFrame.o videoDecodeMain.o
	$(EXEC) $(NVCC) $(ALL_LDFLAGS) $(GENCODE_FLAGS) -o $@ $+ $(LIBRARIES)
	$(EXEC) mkdir -p ./bin/$(TARGET_ARCH)/$(TARGET_OS)/$(BUILD_TYPE)
	$(EXEC) cp $@ ./bin/$(TARGET_ARCH)/$(TARGET_OS)/$(BUILD_TYPE)

run: build
	$(EXEC) ./videoPP

clean:
	rm -f videoPP NvHWEncoder.o FrameQueue.o NvHWDecoder.o cudaProcessFrame.o videoDecodeMain.o  data/$(PTX_FILE) $(PTX_FILE)
	rm -rf bin/$(TARGET_ARCH)/$(TARGET_OS)/$(BUILD_TYPE)/videoPP
	rm -rf bin/$(TARGET_ARCH)/$(TARGET_OS)/$(BUILD_TYPE)/$(PTX_FILE)

clobber: clean
