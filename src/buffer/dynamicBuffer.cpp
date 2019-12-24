#include "dynamicBuffer.h"
#include <sstream>
#include <spdlog/spdlog.h>

using namespace std;

namespace mapper
{
namespace buffer
{

DynamicBuffer::DynamicBuffer()
    : mBuffer(nullptr),
      mpFreePos(0)
{
}

DynamicBuffer::~DynamicBuffer()
{
    if (mBuffer)
    {
        free(mBuffer);
        mBuffer = nullptr;
    }
};

DynamicBuffer *DynamicBuffer::alloc(uint32_t capacity)
{
    assert(capacity);

    DynamicBuffer *pDynamicBuffer = new DynamicBuffer();
    if (!pDynamicBuffer)
    {
        spdlog::error("[DynamicBuffer::alloc] create object fail.");
        return nullptr;
    }

    uint32_t allocSize = capacity + BUFBLK_HEAD_SIZE;
    spdlog::trace("[DynamicBuffer::alloc] capacity: {}", allocSize);
    pDynamicBuffer->mBuffer = static_cast<char *>(malloc(allocSize));
    if (pDynamicBuffer->mBuffer == nullptr)
    {
        spdlog::error("[DynamicBuffer::alloc] malloc {} bytes fail.", allocSize);
        delete pDynamicBuffer;
        return nullptr;
    }
    else
    {
        pDynamicBuffer->mpFreePos = static_cast<BufBlk_t *>(pDynamicBuffer->mBuffer);
        pDynamicBuffer->mpFreePos->init(capacity, nullptr, nullptr);

        return pDynamicBuffer;
    }
}

void DynamicBuffer::release(DynamicBuffer *pDynamicBuffer)
{
    delete pDynamicBuffer;
}

void *DynamicBuffer::getBuffer(int reserve)
{
    if (mpFreePos == nullptr)
    {
        // 已无可用缓冲区
        return nullptr;
    }
    else if (mpFreePos->size >= reserve)
    {
        // 从当前缓冲区中分配
        return mpFreePos->buffer;
    }
    else
    {
        // 查找可用缓冲区

        // 1. 向后查找
        BufBlk_t *p = mpFreePos->next;
        while (p && (p->inUse || (p->size < reserve)))
        {
            p = p->next;
        }
        if (p)
        {
            // 找到可用缓冲区
            mpFreePos = p;
            return mpFreePos->buffer;
        }
        else
        {
            // 2. 从头查找
            p = static_cast<BufBlk_t *>(mBuffer);
            while (p != mpFreePos && (p->inUse || (p->size < reserve)))
            {
                // 因为 mpFreePos 是属于链表中的某一段
                // 因此从头开始查找时 p 不可能为空
                assert(p);
                p = p->next;
            }
            if (p != mpFreePos)
            {
                // 找到可用缓冲区
                mpFreePos = p;
                return mpFreePos->buffer;
            }
            else
            {
                // 当前无可用缓冲区
                return nullptr;
            }
        }
    }
}

void DynamicBuffer::cutBuffer(uint32_t size)
{
    assert(mpFreePos && size <= mpFreePos->size && mpFreePos->inUse == false);

    // 后一个缓冲区为空，或者已被分配（否则应该与当前缓冲区合并）
    assert(mpFreePos->next == nullptr || mpFreePos->next->inUse);

    // 如果剩余缓冲区大小 小于或等于 缓冲区结构体头部大小，
    // 则当前所有缓冲区都被分配出去。

    // 被割取的不包含缓冲区结构体头部长度的大小
    uint32_t cutSize = (mpFreePos->size - size) > BUFBLK_HEAD_SIZE
                           ? size
                           : mpFreePos->size;
    uint32_t remainSize = mpFreePos->size - cutSize;

    if (remainSize > 0)
    {
        assert(remainSize > BUFBLK_HEAD_SIZE);

        // 分割当前缓冲区
        char *p = reinterpret_cast<char *>(mpFreePos);
        BufBlk_t *nextBlk = reinterpret_cast<BufBlk_t *>(p +
                                                         BUFBLK_HEAD_SIZE +
                                                         cutSize);
        nextBlk->init(remainSize - BUFBLK_HEAD_SIZE, mpFreePos, mpFreePos->next);

        mpFreePos->size = cutSize;
        mpFreePos->next = nextBlk;
        mpFreePos->inUse = true;

        mpFreePos = nextBlk;
    }
    else
    {
        // mpFreePos 所指向的缓冲区全部被分配出去
        // 此时 mpFreePos 所指向数据块中的数据无需修改
        // 全部标记为分配给此次割取

        // 查找可用缓冲区

        // 1. 向后查找
        BufBlk_t *p = mpFreePos->next;
        while (p && p->inUse)
        {
            p = p->next;
        }
        if (p)
        {
            // 找到可用缓冲区
            mpFreePos = p;
        }
        else
        {
            // 2. 从头查找
            p = static_cast<BufBlk_t *>(mBuffer);
            while (p != mpFreePos && p->inUse)
            {
                // 因为 mpFreePos 是属于链表中的某一段
                // 因此从头开始查找时 p 不可能为空
                assert(p);
                p = p->next;
            }
            if (p != mpFreePos)
            {
                // 找到可用缓冲区
                mpFreePos = p;
            }
            else
            {
                // 当前无可用缓冲区
                mpFreePos = nullptr;
            }
        }
    }
}

void DynamicBuffer::releaseBuffer(void *pBuffer)
{
    char *p = static_cast<char *>(pBuffer);
    BufBlk_t *pBlk = reinterpret_cast<BufBlk_t *>(p - BUFBLK_HEAD_SIZE);
    assert(pBlk->startFlag == BUFBLK_HEAD_START_FLAG);

    BufBlk_t *prev = pBlk->prev;
    if (prev && prev->inUse == false)
    {
        // 向前合并
        prev->size += BUFBLK_HEAD_SIZE + pBlk->size;
    }
    else
    {
        BufBlk_t *next = pBlk->next;
        if (next && next->inUse == false)
        {
            // 向后合并
            pBlk->size += BUFBLK_HEAD_SIZE + next->size;
            // 调整 mpFreePos
            if (mpFreePos == next)
            {
                mpFreePos = pBlk;
            }
        }
        else
        {
            // 调整 mpFreePos
            if (mpFreePos == nullptr)
            {
                mpFreePos = pBlk;
            }
        }
    }
}

} // namespace buffer
} // namespace mapper
