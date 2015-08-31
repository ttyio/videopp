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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cudaProcessFrame.h"

#define MUL(x,y)    (x*y)

__device__ void YUV2RGB(uint32 *yuvi, float *rgb)
{
    float luma, chromaCb, chromaCr;

    luma     = (float)yuvi[0];
    chromaCb = (float)((int32)yuvi[1] - 512.0f);
    chromaCr = (float)((int32)yuvi[2] - 512.0f);

    
    rgb[0] = MUL(luma,     1.1644) +
            MUL(chromaCb, 0) +
            MUL(chromaCr, 1.5960);
    rgb[1] = MUL(luma,     1.1644) +
            MUL(chromaCb, -0.3918) +
            MUL(chromaCr, -0.8130);
    rgb[2] = MUL(luma,    1.1644) +
            MUL(chromaCb, 2.0172) +
            MUL(chromaCr, 0);
}

__device__ void RGB2YUV(uint32 *rgb, float *yuv)
{

    float r = (float)rgb[0];
    float g = (float)rgb[1];
    float b = (float)rgb[2];

    yuv[0]  = MUL(r, 0.2568) +
            MUL(g, 0.5041) +
            MUL(b, 0.0979) + 16;
    yuv[1]= MUL(r, -0.1482) +
            MUL(g, -0.2910) +
            MUL(b, 0.4392) + 128;
    yuv[2] = MUL(r, 0.4392) +
            MUL(g, -0.3678) +
            MUL(b, -0.0714) + 128;
}


__device__ uint32 RGBAPACK_10bit(float* rgb)
{
    uint32 ARGBpixel = 0;

    rgb[0] = min(max(rgb[0], 0.0f), 1023.f);
    rgb[1] = min(max(rgb[1], 0.0f), 1023.f);
    rgb[2] = min(max(rgb[2], 0.0f), 1023.f);

    ARGBpixel = (((uint32)rgb[2]  >> 2) |
                 (((uint32)rgb[1] >> 2) << 8)  |
                 (((uint32)rgb[0]   >> 2) << 16) | ((uint32)0xff<< 24));

    return  ARGBpixel;
}

__device__ void RGBAUNPACK_10bit(uint32 pixel, uint32* rgb)
{
    rgb[2] = (pixel & 0xFF) << 2;
    rgb[1] = ((pixel>>8) & 0xFF) << 2;
    rgb[0] = ((pixel>>16) & 0xFF) << 2;
}

extern "C" __global__ void NV12ToARGBdrvapi(uint32 *srcImage,     size_t nSourcePitch,
                                  uint32 *dstImage,     size_t nDestPitch,
                                  uint32 width,         uint32 height)
{
    // process 2 pixels per thread
    int32 x = blockIdx.x * (blockDim.x << 1) + (threadIdx.x << 1);
    int32 y = blockIdx.y *  blockDim.y       +  threadIdx.y;
    if (x+1 >= width || y >= height)
        return; 

    uint32 processingPitch = nSourcePitch;
    uint32 dstImagePitch   = nDestPitch >> 2;
    uint8 *srcImageU8     = (uint8 *)srcImage;

    // Read 2 Y components at a time
    uint32 yuvi[6];
    yuvi[0] = (srcImageU8[y * processingPitch + x    ]) << 2;
    yuvi[3] = (srcImageU8[y * processingPitch + x + 1]) << 2;

    uint32 chromaOffset    = processingPitch * height;
    int32 y_chroma = y >> 1;

    if (y & 1)  // odd scanline 
    {
        uint32 chromaCb = srcImageU8[chromaOffset + y_chroma * processingPitch + x    ];
        uint32 chromaCr = srcImageU8[chromaOffset + y_chroma * processingPitch + x + 1];

        if (y_chroma < ((height >> 1) - 1)) // interpolate vertically
        {
            chromaCb = (chromaCb + srcImageU8[chromaOffset + (y_chroma + 1) * processingPitch + x    ] + 1) >> 1;
            chromaCr = (chromaCr + srcImageU8[chromaOffset + (y_chroma + 1) * processingPitch + x + 1] + 1) >> 1;
        }

        yuvi[1] = yuvi[4] = chromaCb << 2;
        yuvi[2] = yuvi[5] = chromaCr << 2;
    }
    else
    {
        yuvi[1] = yuvi[4] = (uint32)srcImageU8[chromaOffset + y_chroma * processingPitch + x    ] << 2;
        yuvi[2] = yuvi[5] = (uint32)srcImageU8[chromaOffset + y_chroma * processingPitch + x + 1] << 2;
    }

    // YUV to RGB Transformation conversion
    float rgb[6];
    YUV2RGB(&yuvi[0], &rgb[0]);
    YUV2RGB(&yuvi[3], &rgb[3]);

    // save to dest
    dstImage[y * dstImagePitch + x     ] = RGBAPACK_10bit(&rgb[0]);
    dstImage[y * dstImagePitch + x + 1 ] = RGBAPACK_10bit(&rgb[3]);
}

extern "C" __global__ void ARGBToNv12drvapi(uint32 *srcImage,     size_t nSourcePitch,
                                  uint32 *dstImage,     size_t nDestPitch,
                                  uint32 width,         uint32 height)
{
    int32 x = blockIdx.x * (blockDim.x << 1) + (threadIdx.x << 1);
    int32 y = blockIdx.y *  blockDim.y       +  threadIdx.y;
    if (x+1 >= width || y >= height)
        return; 

    uint32 processingPitch = nSourcePitch>>2;
    uint8 *dstImageU8     = (uint8 *)dstImage;

    uint32 rgb[6];
    float yuv[6];

    // Clamp the results to RGBA
    RGBAUNPACK_10bit(srcImage[y * processingPitch + x     ], &rgb[0]);
    RGBAUNPACK_10bit(srcImage[y * processingPitch + x + 1 ], &rgb[3]);

    // YUV to RGB Transformation conversion
    RGB2YUV(&rgb[0], &yuv[0]);
    RGB2YUV(&rgb[3], &yuv[3]);

    yuv[0]   = min(max(yuv[0]/4,   0.0f), 255.f);
    yuv[1]   = min(max((yuv[1])/4,   0.0f), 255.f);
    yuv[2]   = min(max((yuv[2])/4,   0.0f), 255.f);
    yuv[3]   = min(max(yuv[3]/4,   0.0f), 255.f);
    yuv[4]   = min(max((yuv[4])/4,   0.0f), 255.f);
    yuv[5]   = min(max((yuv[5])/4,   0.0f), 255.f);

    uint32 dstImagePitch   = nDestPitch;
    dstImageU8[y * dstImagePitch + x] = ((uint32)yuv[0]);
    dstImageU8[y * dstImagePitch + x + 1] = ((uint32)yuv[3]);

    if (y & 1){
    } else {
        int32 y_chroma = y >> 1;
        uint32 chromaOffset    = dstImagePitch* height;
        dstImageU8[chromaOffset + y_chroma * dstImagePitch + x] =  (uint8)((uint32)yuv[1]);
        dstImageU8[chromaOffset + y_chroma * dstImagePitch + x+1] =  (uint8)((uint32)yuv[2]);
    }
}

extern "C" __global__ void ARGBpostprocess(uint32 *srcImage, size_t pitch, uint32 width, uint32 height)
{
    int32 x = blockIdx.x *  blockDim.x + threadIdx.x;
    int32 y = blockIdx.y *  blockDim.y + threadIdx.y;
    if (x >= width || y >= height)
        return; 

    uint32 processingPitch = pitch>>2;
    uint32 rgb[3];
    RGBAUNPACK_10bit(srcImage[y*processingPitch + x], rgb);

    //todo

    srcImage[y*processingPitch + x] = RGBAPACK_10bit((float*)rgb);
}


