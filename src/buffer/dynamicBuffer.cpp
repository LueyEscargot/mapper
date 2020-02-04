#include "dynamicBuffer.h"
#include <assert.h>
#include <sstream>
#include <spdlog/spdlog.h>

#define ENABLE_PERFORMANCE_MODE
#undef ENABLE_PERFORMANCE_MODE

using namespace std;

namespace mapper
{
namespace buffer
{

const uint64_t DynamicBuffer::BUFBLK_HEAD_SIZE = sizeof(BufBlk_t);
const uint64_t DynamicBuffer::UNIT_ALLIGN_BIT_WIDTH = 7;
const uint64_t DynamicBuffer::UNIT_ALLIGN_SIZE = 1 << UNIT_ALLIGN_BIT_WIDTH;
const uint64_t DynamicBuffer::UNIT_ALLIGN_FIELD_MASK = UNIT_ALLIGN_SIZE - 1;
const uint64_t DynamicBuffer::UNIT_ALLIGN_MASK = ~UNIT_ALLIGN_FIELD_MASK;
const uint32_t DynamicBuffer::MIN_BLK_HEAD_BODY_LENGTH = UNIT_ALLIGN_SIZE;

DynamicBuffer::DynamicBuffer()
    : mBuffer(nullptr),
      mpFreePos(nullptr),
      mInUseCount(0),
      mTotalInUse(0),
      mTotalFree(0)
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

DynamicBuffer *DynamicBuffer::allocDynamicBuffer(uint64_t capacity)
{
    uint64_t alignedCapacity = sizeAllign(capacity);
    assert(alignedCapacity > MIN_BLK_HEAD_BODY_LENGTH);

    DynamicBuffer *pDynamicBuffer = new DynamicBuffer();
    if (!pDynamicBuffer)
    {
        spdlog::error("[DynamicBuffer::allocDynamicBuffer] create object fail.");
        return nullptr;
    }

    spdlog::trace("[DynamicBuffer::allocDynamicBuffer] capacity: {}", alignedCapacity);
    pDynamicBuffer->mBuffer = (char *)malloc(alignedCapacity);
    if (pDynamicBuffer->mBuffer == nullptr)
    {
        spdlog::error("[DynamicBuffer::allocDynamicBuffer] malloc {} bytes fail.",
                      alignedCapacity);
        delete pDynamicBuffer;
        return nullptr;
    }
    else
    {
        pDynamicBuffer->mpFreePos = (BufBlk_t *)pDynamicBuffer->mBuffer;
        pDynamicBuffer->mpFreePos->init(pDynamicBuffer);
        pDynamicBuffer->mpFreePos->__innerBlockSize = alignedCapacity;
        pDynamicBuffer->mTotalFree = alignedCapacity;

        return pDynamicBuffer;
    }
}

void DynamicBuffer::releaseDynamicBuffer(DynamicBuffer *pDynamicBuffer)
{
    delete pDynamicBuffer;
}

string DynamicBuffer::dumpBlk(BufBlk_t *p)
{
    if (p)
    {
        stringstream ss;

        ss << "("
           << (void *)p->__innerPrev << ":" << (p->__innerPrev ? (p->__innerPrev->inUse ? "U" : "F") : "-")
           << ")<-("
           << (void *)p << ":" << (p->inUse ? "U" : "F")
           << ")->("
           << (void *)p->__innerNext << ":" << (p->__innerNext ? (p->__innerNext->inUse ? "U" : "F") : "-")
           << ")";

        return ss.str();
    }
    else
    {
        return "(0:-)";
    }
}

char *DynamicBuffer::reserve(int req_size)
{
    if (mpFreePos == nullptr)
    {
        // 已无可用缓冲区
        return nullptr;
    }

    uint64_t size = req_size + BUFBLK_HEAD_SIZE;
    if (mpFreePos->__innerBlockSize >= size)
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
        while (p && (p->inUse || (p->__innerBlockSize < size)))
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
            while (p != mpFreePos && (p->inUse || (p->__innerBlockSize < size)))
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

DynamicBuffer::BufBlk_t *DynamicBuffer::cutNoLock(uint64_t req_size)
{
    // 为避免过多碎片，每次分配最小必须达到 UNIT_ALLIGN_BIT_WIDTH 中定义的长度
    uint64_t allocSize = sizeAllign(BUFBLK_HEAD_SIZE + req_size);

    auto cutBlock = mpFreePos;

    // 如果剩余缓冲区大小 小于或等于 MIN_BLK_HEAD_BODY_LENGTH 中定义的长度，
    // 则当前所有缓冲区都被分配出去。

    uint64_t remainSize = mpFreePos->__innerBlockSize - allocSize;

#ifdef ENABLE_PERFORMANCE_MODE
    assert(mpFreePos && !mpFreePos->inUse);

    // 相邻缓冲区为空，或者已被分配（否则应该与当前缓冲区合并）
    assert((mpFreePos->__innerPrev == nullptr || mpFreePos->__innerPrev->inUse) ||
           (mpFreePos->__innerNext == nullptr || mpFreePos->__innerNext->inUse));

    assert(allocSize <= mpFreePos->__innerBlockSize);
#endif // ENABLE_PERFORMANCE_MODE

    if (remainSize >= MIN_BLK_HEAD_BODY_LENGTH)
    {
        // 分割当前缓冲区
        auto p = (char *)mpFreePos;
        auto nextBlk = (BufBlk_t *)(p + allocSize);

        nextBlk->init(this);
        nextBlk->__innerBlockSize = remainSize;
        nextBlk->__innerPrev = mpFreePos;
        nextBlk->__innerNext = mpFreePos->__innerNext;
        // 当前节点的后指针指向新建节点
        mpFreePos->__innerNext = nextBlk;
        // 新建节点的后节点不为空时，其前指针指向新建节点
        if (nextBlk->__innerNext)
        {
            nextBlk->__innerNext->__innerPrev = nextBlk;
        }

        // 将前节点 mpFreePos（即未分割之前的节点，也是被割取的节点）的大小调整（缩小）为被割取大小
        mpFreePos->__innerBlockSize = allocSize;

        // 将 mpFreePos 指针指向新建节点
        mpFreePos = nextBlk;

#ifdef ENABLE_PERFORMANCE_MODE
        // nextBlk->__innerBlockSize 必然是大小符合对齐要求的
        assert((nextBlk->__innerBlockSize & UNIT_ALLIGN_FIELD_MASK) == 0);
#endif // ENABLE_PERFORMANCE_MODE
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

#ifdef ENABLE_PERFORMANCE_MODE
    assert(!mpFreePos || mpFreePos->__innerBlockSize >= MIN_BLK_HEAD_BODY_LENGTH);
#endif // ENABLE_PERFORMANCE_MODE

    // 缓冲区分配出去的空间
    mTotalFree -= cutBlock->__innerBlockSize;
    mTotalInUse += cutBlock->__innerBlockSize;
    ++mInUseCount;

#ifdef ENABLE_PERFORMANCE_MODE
    // spdlog::debug("[DynamicBuffer::cutNoLock] mInUseCount[{}] - {}", mInUseCount, dumpBlk(cutBlock));
    assert(mTotalFree >= 0);
#endif // ENABLE_PERFORMANCE_MODE

    cutBlock->inUse = true;
    cutBlock->dataSize = req_size;
    cutBlock->sent = 0;

    return cutBlock;
}

DynamicBuffer::BufBlk_t *DynamicBuffer::getBufBlk(uint64_t size)
{
    lock_guard<mutex> lg(mAccessMutex);

    if (reserve(size))
    {
        return cutNoLock(size);
    }
    else
    {
        return nullptr;
    }
}

void DynamicBuffer::release(BufBlk_t *pBlk)
{
    lock_guard<mutex> lg(mAccessMutex);

#ifdef ENABLE_PERFORMANCE_MODE
    assert(pBlk && pBlk->inUse);
    assert(pBlk->__dynamicBufferObj == this);
#endif // ENABLE_PERFORMANCE_MODE

    --mInUseCount;

    mTotalFree += pBlk->__innerBlockSize;
    mTotalInUse -= pBlk->__innerBlockSize;

    pBlk->inUse = false;
    if (mpFreePos == nullptr)
    {
        // 调整 mpFreePos
        mpFreePos = pBlk;
        assert(mpFreePos->__innerNext == nullptr || mpFreePos->__innerNext->inUse);

        return;
    }

    // 向后合并（即：释放后一个节点，将之合并到当前节点）
    mergeNext(pBlk);

    // 向前合并（即：释放当前节点，将之合并到前一个节点）
    mergePrev(pBlk);

#ifdef ENABLE_PERFORMANCE_MODE
    assert(check());
#endif // ENABLE_PERFORMANCE_MODE
}

bool DynamicBuffer::check()
{
    // check link
    auto p = (BufBlk_t *)mBuffer;
    while (p)
    {
        if (!p->inUse &&
            p->__innerNext &&
            !p->__innerNext->inUse)
        {
            spdlog::error("[DynamicBuffer::check] fail: {}", dumpBlk(p));
            return false;
        }

        // p->__innerBlockSize 必然是大小符合对齐要求的
        assert((p->__innerBlockSize & UNIT_ALLIGN_FIELD_MASK) == 0);

        p = p->__innerNext;
    }

    // check statistic value
    if (mInUseCount < 0)
    {
        spdlog::error("[DynamicBuffer::check] invalid in use count: {}", mInUseCount);
        return false;
    }

    // check mpFreePos
    if (mpFreePos->__innerPrev && !mpFreePos->__innerPrev->inUse)
    {
        spdlog::error("[DynamicBuffer::check] mpFreePos->__innerPrev->inUse is false");
        return false;
    };
    if (mpFreePos->__innerNext)
    {
        spdlog::error("[DynamicBuffer::check] mpFreePos->__innerNext->inUse is false");
        return false;
    };

    return true;
}

uint64_t DynamicBuffer::sizeAllign(const uint64_t size)
{
    auto s = size & UNIT_ALLIGN_MASK;
    s += (size & UNIT_ALLIGN_FIELD_MASK) ? UNIT_ALLIGN_SIZE : 0;
    return s;
}

void DynamicBuffer::mergePrev(BufBlk_t *p)
{
    // 向前合并（即：释放当前节点，将之合并到前一个节点）
    BufBlk_t *prevItem = p->__innerPrev;
    if (prevItem && prevItem->inUse == false)
    {
        prevItem->__innerBlockSize += p->__innerBlockSize;
        prevItem->__innerNext = p->__innerNext;
        if (prevItem->__innerNext)
        {
            prevItem->__innerNext->__innerPrev = prevItem;
        }

        if (mpFreePos == p)
        {
            // 调整 mpFreePos
            mpFreePos = prevItem;
        }
    }
}

void DynamicBuffer::mergeNext(BufBlk_t *p)
{
    // 向后合并（即：释放后一个节点，将之合并到当前节点）
    BufBlk_t *nextItem = p->__innerNext;
    if (nextItem && nextItem->inUse == false)
    {
        p->__innerBlockSize += nextItem->__innerBlockSize;
        p->__innerNext = nextItem->__innerNext;
        if (p->__innerNext)
        {
            p->__innerNext->__innerPrev = p;
        }

        if (mpFreePos == nextItem)
        {
            // 调整 mpFreePos
            mpFreePos = p;
        }
    }
}

} // namespace buffer
} // namespace mapper
