/**
 * Copyright 1993-2013 NVIDIA Corporation.  All rights reserved.
 *
 * Please refer to the NVIDIA end user license agreement (EULA) associated
 * with this source code for terms and conditions that govern your use of
 * this software. Any use, reproduction, disclosure, or distribution of
 * this software and related documentation outside the terms of the EULA
 * is strictly prohibited.
 *
 */

// Helper functions for CUDA Driver API error handling (make sure that CUDA_H is included in your projects)
#ifndef HELPER_CUDA_DRVAPI_H
#define HELPER_CUDA_DRVAPI_H

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "nvEncodeAPI.h"

////////////////////////////////////////////////////////////////////////////////
// These are CUDA Helper functions

// add a level of protection to the CUDA SDK samples, let's force samples to explicitly include CUDA.H
// This will output the proper CUDA error strings in the event that a CUDA host call returns an error
#ifndef checkCudaErrors
#define checkCudaErrors(err)  __checkCudaErrors (err, __FILE__, __LINE__)
#define checkNvEncErrors(err)  __checkNvEncErrors (err, __FILE__, __LINE__)

// These are the inline versions for all of the SDK helper functions
inline void __checkCudaErrors(CUresult err, const char *file, const int line)
{
    if (CUDA_SUCCESS != err)
    {
        fprintf(stderr, "checkCudaErrors() Driver API error = %04d from file <%s>, line %i.\n", err, file, line);
        exit(EXIT_FAILURE);
    }
}

inline void __checkNvEncErrors(NVENCSTATUS err, const char *file, const int line)
{
    if (NV_ENC_SUCCESS != err)
    {
        fprintf(stderr, "checkNvEncErrors() Driver API error = %04d from file <%s>, line %i.\n", err, file, line);
        exit(EXIT_FAILURE);
    }
}
#endif

#endif
