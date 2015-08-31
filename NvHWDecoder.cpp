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

#include "NvHWDecoder.h"
#include "FrameQueue.h"
#include <cuviddec.h>
#include <assert.h>

#define MAX_FRAME_COUNT 2

static int HandleVideoData(void *pUserData, CUVIDSOURCEDATAPACKET *pPacket)
{
    VideoSourceData *pVideoSourceData = (VideoSourceData *)pUserData;

    if (!pVideoSourceData->pFrameQueue->isDecodeFinished())
    {
        CUresult oResult = cuvidParseVideoData(pVideoSourceData->hVideoParser, pPacket);

        if ((pPacket->flags & CUVID_PKT_ENDOFSTREAM) || (oResult != CUDA_SUCCESS))
            pVideoSourceData->pFrameQueue->endDecode();
    }

    return !pVideoSourceData->pFrameQueue->isDecodeFinished();
}

static int HandleVideoSequence(void *pUserData, CUVIDEOFORMAT *pFormat)
{
    return 1;
}

static int HandlePictureDecode(void *pUserData, CUVIDPICPARAMS *pPicParams)
{
    VideoSourceData *pVideoSourceData = (VideoSourceData *)pUserData;

    bool bFrameAvailable = pVideoSourceData->pFrameQueue->waitUntilFrameAvailable(pPicParams->CurrPicIdx);

    if (!bFrameAvailable)
        return false;

    CUresult oResult = cuvidDecodePicture(pVideoSourceData->hVideoDecoder, pPicParams);
    assert(CUDA_SUCCESS == oResult);
    return true;
}

static int HandlePictureDisplay(void *pUserData, CUVIDPARSERDISPINFO *pPicParams)
{
    VideoSourceData *pVideoSourceData = (VideoSourceData *)pUserData;

    pVideoSourceData->pFrameQueue->enqueue(pPicParams);

    return 1;
}

bool CNvHWDecoder::createVideoParser()
{
    // video parser
    CUVIDPARSERPARAMS oVideoParserParameters;
    memset(&oVideoParserParameters, 0, sizeof(CUVIDPARSERPARAMS));
    oVideoParserParameters.CodecType              = oVideoDecodeCreateInfo_.CodecType;
    oVideoParserParameters.ulMaxNumDecodeSurfaces = oVideoDecodeCreateInfo_.ulNumDecodeSurfaces;
    oVideoParserParameters.ulMaxDisplayDelay      = 1;  // this flag is needed so the parser will push frames out to the decoder as quickly as it can
    oVideoParserParameters.pUserData              = &oSourceData_;
    oVideoParserParameters.pfnSequenceCallback    = HandleVideoSequence;    // Called before decoding frames and/or whenever there is a format change
    oVideoParserParameters.pfnDecodePicture       = HandlePictureDecode;    // Called when a picture is ready to be decoded (decode order)
    oVideoParserParameters.pfnDisplayPicture      = HandlePictureDisplay;   // Called whenever a picture is ready to be displayed (display order)
    CUresult oResult = cuvidCreateVideoParser(&oSourceData_.hVideoParser, &oVideoParserParameters);
    return CUDA_SUCCESS == oResult;
}

bool CNvHWDecoder::createVideoDecoder()
{
    CUVIDEOFORMAT rVideoFormat = format();

    memset(&oVideoDecodeCreateInfo_, 0, sizeof(CUVIDDECODECREATEINFO));
    oVideoDecodeCreateInfo_.CodecType           = rVideoFormat.codec;
    oVideoDecodeCreateInfo_.ulWidth             = rVideoFormat.coded_width;
    oVideoDecodeCreateInfo_.ulHeight            = rVideoFormat.coded_height;
    oVideoDecodeCreateInfo_.ulNumDecodeSurfaces = FrameQueue::cnMaximumSize;

    // Limit decode memory to 24MB (16M pixels at 4:2:0 = 24M bytes)
    while (oVideoDecodeCreateInfo_.ulNumDecodeSurfaces * rVideoFormat.coded_width * rVideoFormat.coded_height > 16*1024*1024)
    {
        oVideoDecodeCreateInfo_.ulNumDecodeSurfaces--;
    }

    oVideoDecodeCreateInfo_.ChromaFormat        = rVideoFormat.chroma_format;
    oVideoDecodeCreateInfo_.OutputFormat        = cudaVideoSurfaceFormat_NV12;
    oVideoDecodeCreateInfo_.DeinterlaceMode     = cudaVideoDeinterlaceMode_Adaptive;

    // No scaling
    oVideoDecodeCreateInfo_.ulTargetWidth       = oVideoDecodeCreateInfo_.ulWidth;
    oVideoDecodeCreateInfo_.ulTargetHeight      = oVideoDecodeCreateInfo_.ulHeight;
    oVideoDecodeCreateInfo_.ulNumOutputSurfaces = MAX_FRAME_COUNT;  
    oVideoDecodeCreateInfo_.ulCreationFlags     = cudaVideoCreate_PreferCUVID;
    oVideoDecodeCreateInfo_.vidLock             = hContextLock;

    // create the decoder
    CUresult oResult = cuvidCreateDecoder(&oSourceData_.hVideoDecoder, &oVideoDecodeCreateInfo_);
    assert(CUDA_SUCCESS == oResult);
}

bool CNvHWDecoder::createVideoSource(const std::string& sFileName)
{
    CUVIDSOURCEPARAMS oVideoSourceParameters;
    memset(&oVideoSourceParameters, 0, sizeof(CUVIDSOURCEPARAMS));
    oVideoSourceParameters.pUserData = &oSourceData_;               
    oVideoSourceParameters.pfnVideoDataHandler = HandleVideoData;   
    oVideoSourceParameters.pfnAudioDataHandler = 0;
    CUresult oResult = cuvidCreateVideoSource(&oSourceData_.hVideoSource, sFileName.c_str(), &oVideoSourceParameters);
    return (CUDA_SUCCESS == oResult);

}

CNvHWDecoder::CNvHWDecoder(const std::string& sFileName, FrameQueue *pFrameQueue, CUcontext pCudaContext)
{
    oSourceData_.pFrameQueue = pFrameQueue;
    oSourceData_.pContext      = pCudaContext;

    CUresult result = cuvidCtxLockCreate(&hContextLock, pCudaContext);
    if (result != CUDA_SUCCESS) {
        printf("cuvidCtxLockCreate failed: %d\n", result);
        assert(0);
    }

    createVideoSource(sFileName);
    createVideoDecoder();
    createVideoParser();
}

CNvHWDecoder::~CNvHWDecoder()
{
    cuvidCtxLockDestroy(hContextLock);
    cuvidDestroyVideoSource(oSourceData_.hVideoSource);
}

CUVIDEOFORMAT CNvHWDecoder::format() const
{
    CUVIDEOFORMAT oFormat;
    CUresult oResult = cuvidGetSourceVideoFormat(oSourceData_.hVideoSource, &oFormat, 0);
    assert(CUDA_SUCCESS == oResult);

    return oFormat;
}

void CNvHWDecoder::getProgressive(bool &progressive)
{
    CUVIDEOFORMAT rCudaVideoFormat=  format();
    progressive = (rCudaVideoFormat.progressive_sequence != 0);
}

void CNvHWDecoder::start()
{
    CUresult oResult = cuvidSetVideoSourceState(oSourceData_.hVideoSource, cudaVideoState_Started);
    assert(CUDA_SUCCESS == oResult);
}

void CNvHWDecoder::stop()
{
    CUresult oResult = cuvidSetVideoSourceState(oSourceData_.hVideoSource, cudaVideoState_Stopped);
    assert(CUDA_SUCCESS == oResult);
}

unsigned int CNvHWDecoder::sourceWidth() const
{
    return oVideoDecodeCreateInfo_.ulWidth;
}

unsigned int CNvHWDecoder::sourceHeight() const
{
    return oVideoDecodeCreateInfo_.ulHeight;
}

unsigned long CNvHWDecoder::targetWidth() const
{
    return oVideoDecodeCreateInfo_.ulTargetWidth;
}

unsigned long CNvHWDecoder::targetHeight() const
{
    return oVideoDecodeCreateInfo_.ulTargetHeight;
}


void CNvHWDecoder::mapFrame(int iPictureIndex, CUdeviceptr *ppDevice, unsigned int *pPitch, CUVIDPROCPARAMS *pVideoProcessingParameters)
{
    CUresult oResult = cuvidMapVideoFrame(oSourceData_.hVideoDecoder,
                                          iPictureIndex,
                                          ppDevice,
                                          pPitch, pVideoProcessingParameters);
    assert(CUDA_SUCCESS == oResult);
    assert(0 != *ppDevice);
    assert(0 != *pPitch);
}

void CNvHWDecoder::unmapFrame(CUdeviceptr pDevice)
{
    CUresult oResult = cuvidUnmapVideoFrame(oSourceData_.hVideoDecoder, pDevice);
    assert(CUDA_SUCCESS == oResult);
}

CUvideoctxlock CNvHWDecoder::getCtxLock() const
{
    return hContextLock;
}

