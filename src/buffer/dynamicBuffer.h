/**
 * @file dynamicBuffer.h
 * @author Liu Yu (source@liuyu.com)
 * @brief Class for Heap Buffer.
 * @version 1.0
 * @date 2019-12-24
 * 
 * @copyright Copyright (c) 2019
 * 
 */

#ifndef __MAPPER_BUFFER_DYNAMICBUFFER_H__
#define __MAPPER_BUFFER_DYNAMICBUFFER_H__

#include <netinet/in.h>
#include <sys/socket.h>
#include <list>
#include <mutex>
#include <string>
#include <tuple>

namespace mapper
{
namespace buffer
{

class DynamicBuffer
{
public:
    static const uint64_t BUFBLK_HEAD_SIZE;
    static const uint64_t UNIT_ALLIGN_SIZE;
    static const uint64_t UNIT_ALLIGN_FIELD_MASK;
    static const uint64_t UNIT_ALLIGN_MASK;
    static const uint64_t UNIT_ALLIGN_BIT_WIDTH;
    static const uint32_t MIN_BLK_HEAD_BODY_LENGTH;

    struct BufBlk_t
    {
        DynamicBuffer *__dynamicBufferObj;
        BufBlk_t *__innerPrev;
        BufBlk_t *__innerNext;
        uint64_t __innerBlockSize; // 整个数据体大小，包含头部及缓冲区大小

        bool inUse;
        BufBlk_t *prev;
        BufBlk_t *next;
        sockaddr_in srcAddr;
        sockaddr_in dstAddr;
        uint64_t dataSize;
        uint64_t sent;
        char buffer[0];

        inline void init(DynamicBuffer *obj)
        {
            __dynamicBufferObj = obj;
            __innerPrev = nullptr;
            __innerNext = nullptr;
            __innerBlockSize = 0;

            inUse = false;
            prev = nullptr;
            next = nullptr;
            srcAddr = {0};
            dstAddr = {0};
            dataSize = 0;
            sent = 0;
        }
        inline uint64_t getBufSize() { return __innerBlockSize - BUFBLK_HEAD_SIZE; }
    };

protected:
    DynamicBuffer();
    virtual ~DynamicBuffer();

public:
    static DynamicBuffer *allocDynamicBuffer(uint64_t capacity);
    static void releaseDynamicBuffer(DynamicBuffer *pDynamicBuffer);
    static std::string dumpBlk(BufBlk_t *p);

    inline bool empty() { return mpFreePos; }
    inline BufBlk_t *getCurBufBlk() { return mpFreePos; }
    char *reserve(int size);
    inline BufBlk_t *cut(uint64_t size)
    {
        std::lock_guard<std::mutex> lg(mAccessMutex);
        return cutNoLock(size);
    }
    BufBlk_t *cutNoLock(uint64_t size);
    BufBlk_t *getBufBlk(uint64_t size);
    void release(BufBlk_t *pBuffer);

    bool check();

protected:
    static uint64_t sizeAllign(const uint64_t size);

    void mergePrev(BufBlk_t *p);
    void mergeNext(BufBlk_t *p);

    std::mutex mAccessMutex;
    void *mBuffer;
    BufBlk_t *mpFreePos;
    int64_t mTotalBuffer;
    int32_t mInUseCount;
    int64_t mTotalInUse;
    int64_t mTotalFree;
};

} // namespace buffer
} // namespace mapper

#endif // __MAPPER_BUFFER_DYNAMICBUFFER_H__
