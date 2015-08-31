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

#include "FrameQueue.h"
#include <assert.h>

FrameQueue::FrameQueue():
    nReadPosition_(0)
    , nFramesInQueue_(0)
    , bEndOfDecode_(0)
{
    memset(aDisplayQueue_, 0, cnMaximumSize * sizeof(CUVIDPARSERDISPINFO));
    memset((void *)aIsFrameInUse_, 0, cnMaximumSize * sizeof(int));
}

void FrameQueue::enqueue(const CUVIDPARSERDISPINFO *pPicParams)
{
    aIsFrameInUse_[pPicParams->picture_index] = true;

    do
    {
        if (nFramesInQueue_ < (int)FrameQueue::cnMaximumSize)
        {
            int iWritePosition = (nReadPosition_ + nFramesInQueue_) % cnMaximumSize;
            aDisplayQueue_[iWritePosition] = *pPicParams;
            nFramesInQueue_++;
            break;
        }

        sleep(1);
    } while (!bEndOfDecode_);

}

bool FrameQueue::dequeue(CUVIDPARSERDISPINFO *pDisplayInfo)
{
    pDisplayInfo->picture_index = -1;

    if (nFramesInQueue_ > 0)
    {
        int iEntry = nReadPosition_;
        *pDisplayInfo = aDisplayQueue_[iEntry];
        nReadPosition_ = (iEntry+1) % cnMaximumSize;
        nFramesInQueue_--;
        
        return true;
    }

    return false;
}

void FrameQueue::releaseFrame(const CUVIDPARSERDISPINFO *pPicParams)
{
    aIsFrameInUse_[pPicParams->picture_index] = false;
}

bool FrameQueue::isInUse(int nPictureIndex) const
{
    assert(nPictureIndex >= 0);
    assert(nPictureIndex < (int)cnMaximumSize);

    return (0 != aIsFrameInUse_[nPictureIndex]);
}

bool FrameQueue::isDecodeFinished() const
{
    return (!nFramesInQueue_ && bEndOfDecode_);
}

void FrameQueue::endDecode()
{
    bEndOfDecode_ = true;
}


bool FrameQueue::waitUntilFrameAvailable(int nPictureIndex)
{
    while (isInUse(nPictureIndex))
    {
        sleep(1);   

        if (bEndOfDecode_){
            return false;
        }
    }

    return true;
}

