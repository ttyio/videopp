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

#ifndef _CUDAPROCESSFRAME_H_
#define _CUDAPROCESSFRAME_H_

#include <cuda.h>
#include <vector_types.h>

typedef unsigned char   uint8;
typedef unsigned int    uint32;
typedef int             int32;


CUresult loadCUDAModules();

CUresult cudaLaunchNV12toARGBDrv(CUdeviceptr d_srcNV12,   size_t nSourcePitch,
                                 CUdeviceptr d_dstARGB,  size_t nDestPitch,
                                 uint32 width,           uint32 height,
                                 CUstream streamID);

CUresult cudaLaunchARGBpostprocess(CUdeviceptr d_srcARGB,   size_t nSourcePitch,
                                   uint32 width,           uint32 height,
                                   CUstream streamID);

CUresult cudaLaunchARGBtoNV12Drv(CUdeviceptr d_srcARGB,   size_t nSourcePitch,
                                 CUdeviceptr d_dstNV12,  size_t nDestPitch,
                                 uint32 width,           uint32 height,
                                 CUstream streamID);


#endif
