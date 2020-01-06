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

DynamicBuffer *DynamicBuffer::allocDynamicBuffer(uint32_t capacity)
{
    assert(capacity);

    DynamicBuffer *pDynamicBuffer = new DynamicBuffer();
    if (!pDynamicBuffer)
    {
        spdlog::error("[DynamicBuffer::allocDynamicBuffer] create object fail.");
        return nullptr;
    }

    uint32_t allocSize = capacity + BUFBLK_HEAD_SIZE;
    spdlog::trace("[DynamicBuffer::allocDynamicBuffer] capacity: {}", allocSize);
    pDynamicBuffer->mBuffer = (char *)malloc(allocSize);
    if (pDynamicBuffer->mBuffer == nullptr)
    {
        spdlog::error("[DynamicBuffer::allocDynamicBuffer] malloc {} bytes fail.",
                      allocSize);
        delete pDynamicBuffer;
        return nullptr;
    }
    else
    {
        pDynamicBuffer->mpFreePos = (BufBlk_t *)pDynamicBuffer->mBuffer;
        init(pDynamicBuffer->mpFreePos, capacity, nullptr, nullptr);

        return pDynamicBuffer;
    }
}

void DynamicBuffer::releaseDynamicBuffer(DynamicBuffer *pDynamicBuffer)
{
    delete pDynamicBuffer;
}

char *DynamicBuffer::reserve(int size)
{
    if (mpFreePos == nullptr)
    {
        // 已无可用缓冲区
        return nullptr;
    }
    else if (mpFreePos->size >= size)
    {
        // 从当前缓冲区中分配
        mpFreePos->sent = 0;
        return mpFreePos->buffer;
    }
    else
    {
        // 查找可用缓冲区

        // 1. 向后查找
        BufBlk_t *p = mpFreePos->__innerNext;
        while (p && (p->inUse || (p->size < size)))
        {
            p = p->__innerNext;
        }
        if (p)
        {
            // 找到可用缓冲区
            mpFreePos = p;
            mpFreePos->sent = 0;
            return mpFreePos->buffer;
        }
        else
        {
            // 2. 从头查找
            p = (BufBlk_t *)mBuffer;
            while (p != mpFreePos && (p->inUse || (p->size < size)))
            {
                // 因为 mpFreePos 是属于链表中的某一段
                // 因此从头开始查找时 p 不可能为空
                assert(p);
                p = p->__innerNext;
            }
            if (p != mpFreePos)
            {
                // 找到可用缓冲区
                mpFreePos = p;
                mpFreePos->sent = 0;
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

DynamicBuffer::BufBlk_t *DynamicBuffer::cut(uint32_t size)
{
    assert(mpFreePos && size <= mpFreePos->size && mpFreePos->inUse == false);

    // 后一个缓冲区为空，或者已被分配（否则应该与当前缓冲区合并）
    assert(mpFreePos->__innerNext == nullptr || mpFreePos->__innerNext->inUse);

    DynamicBuffer::BufBlk_t *cutBlock = mpFreePos;

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
        init(nextBlk,
             remainSize - BUFBLK_HEAD_SIZE,
             mpFreePos,
             mpFreePos->__innerNext);
        mpFreePos->size = cutSize;
        mpFreePos->__innerNext = nextBlk;
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
        BufBlk_t *p = mpFreePos->__innerNext;
        while (p && p->inUse)
        {
            p = p->__innerNext;
        }
        if (p)
        {
            // 找到可用缓冲区
            mpFreePos = p;
        }
        else
        {
            // 2. 从头查找
            p = (BufBlk_t *)mBuffer;
            while (p != mpFreePos && p->inUse)
            {
                // 因为 mpFreePos 是属于链表中的某一段
                // 因此从头开始查找时 p 不可能为空
                assert(p);
                p = p->__innerNext;
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

    return cutBlock;
}

void DynamicBuffer::release(BufBlk_t *pBlk)
{
    assert(pBlk && pBlk->inUse);

    pBlk->inUse = false;
    if (mpFreePos == nullptr)
    {
        // 调整 mpFreePos
        mpFreePos = pBlk;
        return;
    }

    // 向后合并
    BufBlk_t *nextItem = pBlk->__innerNext;
    if (nextItem && nextItem->inUse == false)
    {
        pBlk->size += BUFBLK_HEAD_SIZE + nextItem->size;
        pBlk->__innerNext = nextItem->__innerNext;
        if (pBlk->__innerNext)
        {
            pBlk->__innerNext->__innerPrev = pBlk;
        }

        if (mpFreePos == nextItem)
        {
            // 调整 mpFreePos
            mpFreePos = pBlk;
        }
    }

    // 向前合并
    BufBlk_t *prevItem = pBlk->__innerPrev;
    if (prevItem && prevItem->inUse == false)
    {
        prevItem->size += BUFBLK_HEAD_SIZE + pBlk->size;
        prevItem->__innerNext = pBlk->__innerNext;
        if (prevItem->__innerNext)
        {
            assert(prevItem->__innerNext->inUse);   // 否则之前向后合并时已被合并

            prevItem->__innerNext->__innerPrev = prevItem;
        }

        if (mpFreePos == pBlk)
        {
            // 调整 mpFreePos
            mpFreePos = prevItem;
        }
    }
}

void DynamicBuffer::init(DynamicBuffer::BufBlk_t *pBlk,
                         uint32_t size,
                         BufBlk_t *innerlink_prev,
                         BufBlk_t *innerlink_next)
{
    pBlk->inUse = false;
    pBlk->__innerPrev = innerlink_prev;
    pBlk->__innerNext = innerlink_next;
    pBlk->prev = nullptr;
    pBlk->next = nullptr;
    pBlk->size = size;
    pBlk->sent = 0;
}

} // namespace buffer
} // namespace mapper
