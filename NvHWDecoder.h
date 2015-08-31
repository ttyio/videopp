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

#ifndef VIDEOSOURCE_H
#define VIDEOSOURCE_H

#include <nvcuvid.h>
#include <string>

class FrameQueue;

struct VideoSourceData
{
    CUvideosource  hVideoSource;
    CUvideoparser  hVideoParser;
    CUvideodecoder hVideoDecoder;
    FrameQueue    *pFrameQueue;			
    CUcontext      pContext;
};

class CNvHWDecoder
{
    public:
        CNvHWDecoder(const std::string& sFileName, FrameQueue *pFrameQueue, CUcontext pCudaContext);
        ~CNvHWDecoder();

        void start();

        void stop();

        void getProgressive(bool &progressive);

        unsigned int sourceWidth() const;

        unsigned int sourceHeight() const;

        unsigned long targetWidth() const;

        unsigned long targetHeight() const;

        void mapFrame(int iPictureIndex, CUdeviceptr *ppDevice, unsigned int *nPitch, CUVIDPROCPARAMS *pVideoProcessingParameters);

        void unmapFrame(CUdeviceptr pDevice);

        CUvideoctxlock getCtxLock() const;

    protected:
        CUVIDEOFORMAT format() const;
            
        bool createVideoSource(const std::string& sFileName);
        bool createVideoDecoder();
        bool createVideoParser();

    private:
        VideoSourceData         oSourceData_;
        CUVIDDECODECREATEINFO   oVideoDecodeCreateInfo_;
        CUvideoctxlock          hContextLock;       
};


#endif // VIDEOSOURCE_H

