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
#include <cuda.h>
#include <cudaProfiler.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <memory>
#include <iostream>
#include <cassert>

#include "helper_timer.h"
#include "helper_cuda_drvapi.h"
#include "FrameQueue.h"
#include "NvHWDecoder.h"
#include "NvHWEncoder.h"
#include "cudaProcessFrame.h"

const char *sAppFilename = "videoPP";

#define VIDEO_SOURCE_FILE "plush1_720p_10s.m2v"
#define VIDEO_TARGET_FILE "output.mp4"

StopWatchInterface *frame_timer = NULL;
StopWatchInterface *global_timer = NULL;
float total_time = 0.0f;

bool                g_bDone       = false;
bool                g_bIsProgressive = true; 

CUcontext          g_oDecContext = 0;
CUcontext          g_oEncContext = 0;

FrameQueue    *g_pFrameQueue   = 0;
CNvHWDecoder  *g_pNvHWDecoder  = 0;

CUdeviceptr    g_pRGBAFrame[2] = { 0, 0 }; 
void*          g_pNV12Frame[2] = { 0, 0 }; 


unsigned int g_FrameCount = 0;
unsigned int g_DecodeFrameCount = 0;
unsigned int g_fpsCount = 0;      // FPS count for averaging
unsigned int g_fpsLimit = 16;     // FPS limit for sampling timer;

#include  "NvHWEncoder.h"

#define MAX_ENCODE_QUEUE 32
#define BITSTREAM_BUFFER_SIZE 2 * 1024 * 1024
CNvHWEncoder                                        *m_pNvHWEncoder;
uint32                                               m_uEncodeBufferCount = 4; // min buffers is numb + 1 + 3 pipelining
EncodeBuffer                                         m_stEncodeBuffer[MAX_ENCODE_QUEUE];
CNvQueue<EncodeBuffer>                               m_EncodeBufferQueue;

void printStatistics()
{
    int   hh, mm, ss, msec;

    float present_fps = 1.f / (total_time / (g_FrameCount * 1000.f));
    float decoded_fps = 1.f / (total_time / (g_DecodeFrameCount * 1000.f));

    msec = ((int)total_time % 1000);
    ss   = (int)(total_time/1000) % 60;
    mm   = (int)(total_time/(1000*60)) % 60;
    hh   = (int)(total_time/(1000*60*60)) % 60;

    printf("\n[%s] statistics\n", sAppFilename);
    printf("\t Video Length (hh:mm:ss.msec)   = %02d:%02d:%02d.%03d\n", hh, mm, ss, msec);

    printf("\t Frames Presented (inc repeats) = %d\n", g_FrameCount);
    printf("\t Average Present Rate     (fps) = %4.2f\n", present_fps);

    printf("\t Frames Decoded   (hardware)    = %d\n", g_DecodeFrameCount);
    printf("\t Average Rate of Decoding (fps) = %4.2f\n", decoded_fps);
}

void computeFPS()
{
    sdkStopTimer(&frame_timer);

    g_fpsCount++;

    char sFPS[256];
    std::string sDecodeStatus = "PLAY\0";

    if (g_fpsCount == g_fpsLimit)
    {
        float ifps = 1.f / (sdkGetAverageTimerValue(&frame_timer) / 1000.f);

        sprintf(sFPS, "%s [%s] - [%3.1f fps, %s %d]",
                sAppFilename, sDecodeStatus.c_str(), ifps,
                (g_bIsProgressive ? "Frame" : "Field"),
                g_DecodeFrameCount);


        printf("[%s] - [%s: %04d, %04.1f fps, frame time: %04.2f (ms) ]\n",
               sAppFilename,
               (g_bIsProgressive ? "Frame" : "Field"),
               g_FrameCount, ifps, 1000.f/ifps);

        sdkResetTimer(&frame_timer);
        g_fpsCount = 0;
    }

    if (g_bDone || g_pFrameQueue->isDecodeFinished())
    {
        sDecodeStatus = "STOP (End of File)\0";

        // we only want to record this once
        if (total_time == 0.0f)
        {
            total_time = sdkGetTimerValue(&global_timer);
        }

        sdkStopTimer(&global_timer);
    }
    else
    {
        sdkStartTimer(&frame_timer);
    }
}


void releaseIOBuffers()
{
    for (uint32_t i = 0; i < m_uEncodeBufferCount; i++)
    {
        m_pNvHWEncoder->NvEncDestroyInputBuffer(m_stEncodeBuffer[i].stInputBfr.hInputSurface);
        m_stEncodeBuffer[i].stInputBfr.hInputSurface = NULL;

        m_pNvHWEncoder->NvEncDestroyBitstreamBuffer(m_stEncodeBuffer[i].stOutputBfr.hBitstreamBuffer);
        m_stEncodeBuffer[i].stOutputBfr.hBitstreamBuffer = NULL;
    }
}

void AllocateIOBuffers(uint32_t uInputWidth, uint32_t uInputHeight)
{
    m_EncodeBufferQueue.Initialize(m_stEncodeBuffer, m_uEncodeBufferCount);
    for (uint32_t i = 0; i < m_uEncodeBufferCount; i++)
    {
        checkNvEncErrors(m_pNvHWEncoder->NvEncCreateInputBuffer(uInputWidth, uInputHeight, &m_stEncodeBuffer[i].stInputBfr.hInputSurface));

        m_stEncodeBuffer[i].stInputBfr.bufferFmt = NV_ENC_BUFFER_FORMAT_NV12_PL;
        m_stEncodeBuffer[i].stInputBfr.dwWidth = uInputWidth;
        m_stEncodeBuffer[i].stInputBfr.dwHeight = uInputHeight;

        checkNvEncErrors(m_pNvHWEncoder->NvEncCreateBitstreamBuffer(BITSTREAM_BUFFER_SIZE, &m_stEncodeBuffer[i].stOutputBfr.hBitstreamBuffer));
        m_stEncodeBuffer[i].stOutputBfr.dwBitstreamBufferSize = BITSTREAM_BUFFER_SIZE;
        m_stEncodeBuffer[i].stOutputBfr.hOutputEvent = NULL;
    }
}

bool openOutputVideo(int width, int height)
{
    uint32_t numBytesRead = 0;
    int numFramesEncoded = 0;

    m_pNvHWEncoder = new CNvHWEncoder;

    checkNvEncErrors(m_pNvHWEncoder->Initialize(g_oEncContext, NV_ENC_DEVICE_TYPE_CUDA));

    checkNvEncErrors(m_pNvHWEncoder->CreateEncoder(VIDEO_TARGET_FILE, NV_ENC_H264, width, height, 30, 5000000));

    AllocateIOBuffers(width, height);

    return 0;
}

bool loadVideoSource(const char *video_file, unsigned int &width, unsigned int &height)
{
    g_pFrameQueue  = new FrameQueue;
    g_pNvHWDecoder = new CNvHWDecoder(video_file, g_pFrameQueue, g_oDecContext);

    width = g_pNvHWDecoder->sourceWidth();
    height = g_pNvHWDecoder->sourceHeight();

    bool IsProgressive = 0;
    g_pNvHWDecoder->getProgressive(IsProgressive);
    return IsProgressive;
}


bool initCudaResources()
{
    printf("\n");

    cuInit(0);
    CUdevice g_oDecDevice  = 0;
    CUdevice g_oEncDevice  = 0;
    checkCudaErrors(cuDeviceGet(&g_oDecDevice, 0));

    // get compute capabilities and the devicename
    int major, minor;
    size_t totalGlobalMem;

    char deviceName[256];
    checkCudaErrors(cuDeviceComputeCapability(&major, &minor, g_oDecDevice));
    checkCudaErrors(cuDeviceGetName(deviceName, 256, g_oDecDevice));
    printf("> Using GPU Device: %s has SM %d.%d compute capability\n", deviceName, major, minor);

    checkCudaErrors(cuCtxCreate(&g_oDecContext, CU_CTX_BLOCKING_SYNC, g_oDecDevice));
    loadCUDAModules();

    // load video source
    unsigned int videoWidth  = 0;
    unsigned int videoHeight = 0;
    g_bIsProgressive = loadVideoSource(VIDEO_SOURCE_FILE, videoWidth, videoHeight);


    // RGBA uint32
    checkCudaErrors(cuMemAlloc(&g_pRGBAFrame[0], g_pNvHWDecoder->targetWidth() * g_pNvHWDecoder->targetHeight() * 4));
    checkCudaErrors(cuMemAlloc(&g_pRGBAFrame[1], g_pNvHWDecoder->targetWidth() * g_pNvHWDecoder->targetHeight() * 4));

    // should be encode_width_align*3/2
    checkCudaErrors(cuMemHostAlloc(&g_pNV12Frame[0], g_pNvHWDecoder->targetWidth() * g_pNvHWDecoder->targetHeight() * 4, CU_MEMHOSTALLOC_PORTABLE|CU_MEMHOSTALLOC_DEVICEMAP));
    checkCudaErrors(cuMemHostAlloc(&g_pNV12Frame[1], g_pNvHWDecoder->targetWidth() * g_pNvHWDecoder->targetHeight() * 4, CU_MEMHOSTALLOC_PORTABLE|CU_MEMHOSTALLOC_DEVICEMAP));
    CUdeviceptr pinDevPtr = NULL;
    checkCudaErrors(cuMemHostGetDevicePointer(&pinDevPtr, g_pNV12Frame[0], 0));
    assert(pinDevPtr == (CUdeviceptr)g_pNV12Frame[0]);
    checkCudaErrors(cuMemHostGetDevicePointer(&pinDevPtr, g_pNV12Frame[1], 0));
    assert(pinDevPtr == (CUdeviceptr)g_pNV12Frame[1]);

    CUcontext cuCurrent = NULL;
    CUresult result = cuCtxPopCurrent(&cuCurrent);
    if (result != CUDA_SUCCESS){
        printf("cuCtxPopCurrent: %d\n", result);
        assert(0);
    }

    checkCudaErrors(cuDeviceGet(&g_oEncDevice, 1));
    checkCudaErrors(cuCtxCreate(&g_oEncContext, CU_CTX_SCHED_AUTO, g_oEncDevice));
    result = cuCtxPopCurrent(&cuCurrent);
    if (result != CUDA_SUCCESS){
        printf("cuCtxPopCurrent: %d\n", result);
        assert(0);
    }

    // open output
    openOutputVideo(videoWidth, videoHeight);

    return true;
}


void freeCudaResources(bool bDestroyContext)
{
    if (g_pNvHWDecoder){
        delete g_pNvHWDecoder;
    }

    if (g_pFrameQueue){
        delete g_pFrameQueue;
    }

    if (bDestroyContext){
        checkCudaErrors(cuCtxDestroy(g_oDecContext));
        g_oDecContext= NULL;

        checkCudaErrors(cuCtxDestroy(g_oEncContext));
        g_oEncContext= NULL;
    }
}



void FlushEncoder()
{
    checkNvEncErrors(m_pNvHWEncoder->NvEncFlushEncoderQueue(NULL));

    EncodeBuffer *pEncodeBufer = m_EncodeBufferQueue.GetPending();
    while (pEncodeBufer)
    {
        m_pNvHWEncoder->ProcessOutput(pEncodeBufer);
        pEncodeBufer = m_EncodeBufferQueue.GetPending();
    }
}

void EncodeHostFrame(void* ppNV12Frame, size_t nDecodedPitch)
{
    uint32 width  = g_pNvHWDecoder->targetWidth();
    uint32 height = g_pNvHWDecoder->targetHeight();
    EncodeBuffer *pEncodeBuffer = m_EncodeBufferQueue.GetAvailable();
    if(!pEncodeBuffer){
        m_pNvHWEncoder->ProcessOutput(m_EncodeBufferQueue.GetPending());
        pEncodeBuffer = m_EncodeBufferQueue.GetAvailable();
    }

    unsigned char *pInputSurface = NULL;
    uint32_t lockedPitch = 0;
    checkNvEncErrors(m_pNvHWEncoder->NvEncLockInputBuffer(pEncodeBuffer->stInputBfr.hInputSurface, (void**)&pInputSurface, &lockedPitch));
    assert(lockedPitch == nDecodedPitch);

    memcpy(pInputSurface, (void*)ppNV12Frame, lockedPitch*height*3/2);

    checkNvEncErrors(m_pNvHWEncoder->NvEncUnlockInputBuffer(pEncodeBuffer->stInputBfr.hInputSurface));

    checkNvEncErrors(m_pNvHWEncoder->NvEncEncodeFrame(pEncodeBuffer, NULL, width, height, NV_ENC_PIC_STRUCT_FRAME));
}

void EncodeDevFrame(CUdeviceptr ppNV12Frame, size_t nDecodedPitch)
{
    uint32 width  = g_pNvHWDecoder->targetWidth();
    uint32 height = g_pNvHWDecoder->targetHeight();
    EncodeBuffer *pEncodeBuffer = m_EncodeBufferQueue.GetAvailable();
    if(!pEncodeBuffer){
        m_pNvHWEncoder->ProcessOutput(m_EncodeBufferQueue.GetPending());
        pEncodeBuffer = m_EncodeBufferQueue.GetAvailable();
    }

    unsigned char *pInputSurface = NULL;
    uint32_t lockedPitch = 0;
    checkNvEncErrors(m_pNvHWEncoder->NvEncLockInputBuffer(pEncodeBuffer->stInputBfr.hInputSurface, (void**)&pInputSurface, &lockedPitch));
    assert(lockedPitch == nDecodedPitch);

    checkCudaErrors(cuMemcpyDtoH(pInputSurface, ppNV12Frame, lockedPitch*height*3/2));

    checkNvEncErrors(m_pNvHWEncoder->NvEncUnlockInputBuffer(pEncodeBuffer->stInputBfr.hInputSurface));

    checkNvEncErrors(m_pNvHWEncoder->NvEncEncodeFrame(pEncodeBuffer, NULL, width, height, NV_ENC_PIC_STRUCT_FRAME));
}


void DecodeFrame(CUdeviceptr ppDecodedFrame, size_t nDecodedPitch,
                        CUdeviceptr ppRGBAFrame, size_t nRGBAPitch,
                        CUdeviceptr ppNV12Frame)
{
    uint32 width  = g_pNvHWDecoder->targetWidth();
    uint32 height = g_pNvHWDecoder->targetHeight();

    // Push the current CUDA context 
    CCtxAutoLock lck(g_pNvHWDecoder->getCtxLock());
    checkCudaErrors(cuCtxPushCurrent(g_oDecContext));


    checkCudaErrors(cudaLaunchNV12toARGBDrv(ppDecodedFrame, nDecodedPitch,
                                      ppRGBAFrame, nRGBAPitch,
                                      width, height, 0));


    checkCudaErrors(cudaLaunchARGBpostprocess(ppRGBAFrame, nRGBAPitch, width, height, 0));

    checkCudaErrors(cudaLaunchARGBtoNV12Drv(ppRGBAFrame, nRGBAPitch,
                                      ppNV12Frame, nDecodedPitch,
                                      width, height, 0));


    checkCudaErrors(cuCtxSynchronize());

    // EncodeDevFrame(ppDecodedFrame, nDecodedPitch);

    // Detach from the Current thread
    checkCudaErrors(cuCtxPopCurrent(NULL));
}

bool processFrame()
{
    CUVIDPARSERDISPINFO oDisplayInfo;

    if (g_pFrameQueue->dequeue(&oDisplayInfo))
    {
        int num_fields = (oDisplayInfo.progressive_frame ? (1) : (2+oDisplayInfo.repeat_first_field));
        g_bIsProgressive = oDisplayInfo.progressive_frame ? true : false;

        for (int active_field=0; active_field<num_fields; active_field++)
        {
            CUVIDPROCPARAMS oVideoProcessingParameters;
            memset(&oVideoProcessingParameters, 0, sizeof(CUVIDPROCPARAMS));
            oVideoProcessingParameters.progressive_frame = oDisplayInfo.progressive_frame;
            oVideoProcessingParameters.second_field      = active_field;
            oVideoProcessingParameters.top_field_first   = oDisplayInfo.top_field_first;
            oVideoProcessingParameters.unpaired_field    = (num_fields == 1);


            // map decoded video frame to CUDA surface
            CUdeviceptr  pDecodedFrame = 0;
            unsigned int nDecodedPitch = 0;
            g_pNvHWDecoder->mapFrame(oDisplayInfo.picture_index, &pDecodedFrame, &nDecodedPitch, &oVideoProcessingParameters);

            printf("%s = %02d, PicIndex = %02d, OutputPTS = %08d\n",
                   (oDisplayInfo.progressive_frame ? "Frame" : "Field"),
                   g_DecodeFrameCount, oDisplayInfo.picture_index, oDisplayInfo.timestamp);
            
            size_t RGBPitch = g_pNvHWDecoder->targetWidth()*4;
            DecodeFrame(pDecodedFrame, nDecodedPitch, g_pRGBAFrame[active_field], RGBPitch, (CUdeviceptr)g_pNV12Frame[active_field]);

            // unmap video frame
            g_pNvHWDecoder->unmapFrame(pDecodedFrame);
            g_pFrameQueue->releaseFrame(&oDisplayInfo);

            EncodeHostFrame((void*)(CUdeviceptr)g_pNV12Frame[active_field], nDecodedPitch);

            g_DecodeFrameCount++;
        }

    }
    else
    {
        // Frame Queue has no frames, we don't compute FPS until we start
        return false;
    }

    if (g_pFrameQueue->isDecodeFinished())
    {
        g_bDone = true;
    }

    return true;
}

bool renderVideoFrame()
{
    if (!g_pFrameQueue){
        return true;
    }

    if (processFrame()){
        g_FrameCount ++;
        computeFPS();
    }
    

    if (g_pFrameQueue->isDecodeFinished()){
        return true; //quit
    }

    return false;
}



// Release all previously initd objects
bool cleanup(bool bDestroyContext)
{
    if (bDestroyContext)
    {
        // Attach the CUDA Context (so we may properly free memroy)
        checkCudaErrors(cuCtxPushCurrent(g_oEncContext));

        if (g_pRGBAFrame[0])
        {
            checkCudaErrors(cuMemFree(g_pRGBAFrame[0]));
            g_pRGBAFrame[0] = 0;
        }

        if (g_pRGBAFrame[1])
        {
            checkCudaErrors(cuMemFree(g_pRGBAFrame[1]));
            g_pRGBAFrame[1] = 0;
        }

        if (g_pNV12Frame[0])
        {
            checkCudaErrors(cuMemFreeHost(g_pNV12Frame[0]));
            g_pNV12Frame[0] = 0;
        }

        if (g_pNV12Frame[1])
        {
            checkCudaErrors(cuMemFreeHost(g_pNV12Frame[1]));
            g_pNV12Frame[1] = 0;
        }

        // Detach from the Current thread
        checkCudaErrors(cuCtxPopCurrent(NULL));
    }

    releaseIOBuffers();
    m_pNvHWEncoder->NvEncDestroyEncoder();

    freeCudaResources(bDestroyContext);

    return true;
}

int main(int argc, char *argv[])
{
    // timer
    sdkCreateTimer(&frame_timer);
    sdkResetTimer(&frame_timer);
    sdkCreateTimer(&global_timer);
    sdkResetTimer(&global_timer);

    // cuda rc
    initCudaResources();

    g_pNvHWDecoder->start();

    // start timer
    sdkStartTimer(&global_timer);
    sdkResetTimer(&global_timer);

    bool bQuit = false;
    while (!bQuit)
    {
        bQuit = renderVideoFrame();
    }
    FlushEncoder();

    g_pFrameQueue->endDecode();
    g_pNvHWDecoder->stop();

    computeFPS();
    printStatistics();

    cleanup(true);
    cuProfilerStop();

    return 0;
}


