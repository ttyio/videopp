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

 
#ifndef FRAMEQUEUE_H
#define FRAMEQUEUE_H
 
#include <nvcuvid.h>
#include <unistd.h>
#include <string.h>
#include "NvHWEncoder.h"

class FrameQueue
{
    public:
        static const unsigned int cnMaximumSize = 20; // MAX_FRM_CNT;

        FrameQueue();

        void enqueue(const CUVIDPARSERDISPINFO *pPicParams);

        bool dequeue(CUVIDPARSERDISPINFO *pDisplayInfo);

        void releaseFrame(const CUVIDPARSERDISPINFO *pPicParams);

        bool isInUse(int nPictureIndex) const;

        bool isDecodeFinished() const;

        void endDecode();

        bool waitUntilFrameAvailable(int nPictureIndex);

    private:
        volatile int        nReadPosition_;
        volatile int        nFramesInQueue_;
        CUVIDPARSERDISPINFO aDisplayQueue_[cnMaximumSize];
        volatile int        aIsFrameInUse_[cnMaximumSize];
        volatile int        bEndOfDecode_;		
};

#define MAX_ENCODE_QUEUE 32

#define SET_VER(configStruct, type) {configStruct.version = type##_VER;}

template<class T>
class CNvQueue {
    T** m_pBuffer;
    unsigned int m_uSize;
    unsigned int m_uPendingCount;
    unsigned int m_uAvailableIdx;
    unsigned int m_uPendingndex;
public:
    CNvQueue(): m_pBuffer(NULL), m_uSize(0), m_uPendingCount(0), m_uAvailableIdx(0),
                m_uPendingndex(0)
    {
    }

    ~CNvQueue()
    {
        delete[] m_pBuffer;
    }

    bool Initialize(T *pItems, unsigned int uSize)
    {
        m_uSize = uSize;
        m_uPendingCount = 0;
        m_uAvailableIdx = 0;
        m_uPendingndex = 0;
        m_pBuffer = new T *[m_uSize];
        for (unsigned int i = 0; i < m_uSize; i++)
        {
            m_pBuffer[i] = &pItems[i];
        }
        return true;
    }


    T * GetAvailable()
    {
        T *pItem = NULL;
        if (m_uPendingCount == m_uSize)
        {
            return NULL;
        }
        pItem = m_pBuffer[m_uAvailableIdx];
        m_uAvailableIdx = (m_uAvailableIdx+1)%m_uSize;
        m_uPendingCount += 1;
        return pItem;
    }

    T* GetPending()
    {
        if (m_uPendingCount == 0) 
        {
            return NULL;
        }

        T *pItem = m_pBuffer[m_uPendingndex];
        m_uPendingndex = (m_uPendingndex+1)%m_uSize;
        m_uPendingCount -= 1;
        return pItem;
    }
};

#endif // FRAMEQUEUE_H
