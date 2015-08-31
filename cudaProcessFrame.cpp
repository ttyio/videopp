/*
 * Copyright 1993-2015 NVIDIA Corporation.  All rights reserved.
 *
 * Please refer to the NVIDIA end user license agreement (EULA) associated
 * with this source code for terms and conditions that govern your use of
 * this software. Any use, reproduction, disclosure, or distribution of
 * this software and related documentation outside the terms of the EULA
 * is strictly prohibited.
 *
 */

/* This example demonstrates how to use the Video Decode Library with CUDA
 * bindings to interop between CUDA and DX9 textures for the purpose of post
 * processing video.
 */

#include "cudaProcessFrame.h"
#include "helper_cuda_drvapi.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <cuda.h>
#include <builtin_types.h>

static CUfunction         g_kernelNV12toARGB         = 0;
static CUfunction         g_kernelARGBtoNV12         = 0;
static CUfunction         g_kernelARGBpostprocess    = 0;

CUresult loadCUDAModules()
{
    CUmodule cuModule_;
    checkCudaErrors(cuModuleLoad(&cuModule_, "videoPP64.ptx"));
    checkCudaErrors(cuModuleGetFunction(&g_kernelNV12toARGB, cuModule_, "NV12ToARGBdrvapi"));
    checkCudaErrors(cuModuleGetFunction(&g_kernelARGBtoNV12, cuModule_, "ARGBToNv12drvapi"));
    checkCudaErrors(cuModuleGetFunction(&g_kernelARGBpostprocess, cuModule_, "ARGBpostprocess"));
}

CUresult  cudaLaunchNV12toARGBDrv(CUdeviceptr d_srcNV12, size_t nSourcePitch,
                                  CUdeviceptr d_dstARGB, size_t nDestPitch,
                                  uint32 width,          uint32 height,
                                  CUstream streamID)
{
    // Each thread will output 2 pixels at a time.
    dim3 block(32,16,1);
    dim3 grid((width+(2*block.x-1))/(2*block.x), (height+(block.y-1))/block.y, 1);

    void *args[] = { &d_srcNV12, &nSourcePitch,
                     &d_dstARGB, &nDestPitch,
                     &width, &height
                   };

    checkCudaErrors(cuLaunchKernel(g_kernelNV12toARGB, grid.x, grid.y, grid.z,
                            block.x, block.y, block.z,
                            0, streamID,
                            args, NULL));


}

CUresult cudaLaunchARGBtoNV12Drv(CUdeviceptr d_srcARGB,   size_t nSourcePitch,
                                 CUdeviceptr d_dstNV12,  size_t nDestPitch,
                                 uint32 width,           uint32 height,
                                 CUstream streamID)
{
    // Each thread will output 2 pixels at a time.
    dim3 block(32,16,1);
    dim3 grid((width+(2*block.x-1))/(2*block.x), (height+(block.y-1))/block.y, 1);

    void *args[] = { &d_srcARGB, &nSourcePitch,
                     &d_dstNV12, &nDestPitch,
                     &width, &height
                   };

    checkCudaErrors(cuLaunchKernel(g_kernelARGBtoNV12, grid.x, grid.y, grid.z,
                            block.x, block.y, block.z,
                            0, streamID,
                            args, NULL));
}

CUresult cudaLaunchARGBpostprocess(CUdeviceptr d_srcARGB,   size_t nSourcePitch,
                                   uint32 width,           uint32 height,
                                   CUstream streamID)
{

    dim3 block(32,32,1);
    dim3 grid((width+(block.x-1))/(block.x), (height+(block.y-1))/block.y, 1);

    void *args[] = { &d_srcARGB, &nSourcePitch, &width, &height };

    checkCudaErrors(cuLaunchKernel(g_kernelARGBpostprocess, grid.x, grid.y, grid.z,
                            block.x, block.y, block.z,
                            0, streamID,
                            args, NULL));
}

