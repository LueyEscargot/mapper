#include "dynamicBuffer.h"
#include <sstream>
#include <spdlog/spdlog.h>

using namespace std;

namespace mapper
{
namespace buffer
{

const uint64_t DynamicBuffer::BUFBLK_HEAD_SIZE = sizeof(BufBlk_t);
const uint32_t DynamicBuffer::ALLOC_UNIT_SIZE = 128;
const uint32_t DynamicBuffer::MIN_BLK_HEAD_BODY_LENGTH = ALLOC_UNIT_SIZE;
const uint32_t DynamicBuffer::MIN_BLK_BODY_LENGTH = MIN_BLK_HEAD_BODY_LENGTH - BUFBLK_HEAD_SIZE;

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
    assert(capacity > MIN_BLK_HEAD_BODY_LENGTH);

    DynamicBuffer *pDynamicBuffer = new DynamicBuffer();
    if (!pDynamicBuffer)
    {
        spdlog::error("[DynamicBuffer::allocDynamicBuffer] create object fail.");
        return nullptr;
    }

    spdlog::trace("[DynamicBuffer::allocDynamicBuffer] capacity: {}", capacity);
    pDynamicBuffer->mBuffer = (char *)malloc(capacity);
    if (pDynamicBuffer->mBuffer == nullptr)
    {
        spdlog::error("[DynamicBuffer::allocDynamicBuffer] malloc {} bytes fail.",
                      capacity);
        delete pDynamicBuffer;
        return nullptr;
    }
    else
    {
        pDynamicBuffer->mpFreePos = (BufBlk_t *)pDynamicBuffer->mBuffer;
        pDynamicBuffer->mpFreePos->init();
        pDynamicBuffer->mpFreePos->__innerBlockSize = capacity;
        pDynamicBuffer->mTotalFree = capacity;

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

DynamicBuffer::BufBlk_t *DynamicBuffer::cut(uint64_t req_size)
{
    assert(mpFreePos &&
           req_size <= (mpFreePos->__innerBlockSize - BUFBLK_HEAD_SIZE) &&
           mpFreePos->inUse == false);

    // 相邻缓冲区为空，或者已被分配（否则应该与当前缓冲区合并）
    assert((mpFreePos->__innerPrev == nullptr || mpFreePos->__innerPrev->inUse) ||
           (mpFreePos->__innerNext == nullptr || mpFreePos->__innerNext->inUse));

    // 为避免过多碎片，每次分配最小必须达到 ALLOC_UNIT_SIZE 中定义的长度
    uint64_t allocSize = req_size < MIN_BLK_BODY_LENGTH
                             ? MIN_BLK_HEAD_BODY_LENGTH
                             : BUFBLK_HEAD_SIZE + req_size;

    auto cutBlock = mpFreePos;

    // 如果剩余缓冲区大小 小于或等于 MIN_BLK_HEAD_BODY_LENGTH 中定义的长度，
    // 则当前所有缓冲区都被分配出去。

    uint64_t remainSize = mpFreePos->__innerBlockSize - allocSize;

    if (remainSize >= MIN_BLK_HEAD_BODY_LENGTH)
    {
        // 分割当前缓冲区
        auto p = (char *)mpFreePos;
        auto nextBlk = (BufBlk_t *)(p + allocSize);

        nextBlk->init();
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

    assert(!mpFreePos || mpFreePos->__innerBlockSize >= MIN_BLK_HEAD_BODY_LENGTH);

    // 缓冲区分配出去的空间
    mTotalFree -= cutBlock->__innerBlockSize;
    mTotalInUse += cutBlock->__innerBlockSize;
    ++mInUseCount;
    // spdlog::debug("[DynamicBuffer::cut] mInUseCount[{}] - {}", mInUseCount, dumpBlk(cutBlock));
    assert(mTotalFree >= 0);

    cutBlock->inUse = true;
    cutBlock->dataSize = req_size;
    cutBlock->sent = 0;

    return cutBlock;
}

void DynamicBuffer::release(BufBlk_t *pBlk)
{
    assert(pBlk && pBlk->inUse);

    --mInUseCount;
    assert(mInUseCount >= 0);

    mTotalFree += pBlk->__innerBlockSize;
    mTotalInUse -= pBlk->__innerBlockSize;

    // assert(healthCheck());

    pBlk->inUse = false;
    if (mpFreePos == nullptr)
    {
        // 调整 mpFreePos
        mpFreePos = pBlk;
        assert(mpFreePos->__innerNext == nullptr || mpFreePos->__innerNext->inUse);

        return;
    }

    // 向后合并（即：释放后一个节点，将之合并到当前节点）
    BufBlk_t *nextItem = pBlk->__innerNext;
    if (nextItem && nextItem->inUse == false)
    {
        pBlk->__innerBlockSize += nextItem->__innerBlockSize;
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

    // 向前合并（即：释放当前节点，将之合并到前一个节点）
    BufBlk_t *prevItem = pBlk->__innerPrev;
    if (prevItem && prevItem->inUse == false)
    {
        prevItem->__innerBlockSize +=  pBlk->__innerBlockSize;
        prevItem->__innerNext = pBlk->__innerNext;
        if (prevItem->__innerNext)
        {
            assert(prevItem->__innerNext->inUse); // 否则之前向后合并时已被合并

            prevItem->__innerNext->__innerPrev = prevItem;
        }

        if (mpFreePos == pBlk)
        {
            // 调整 mpFreePos
            mpFreePos = prevItem;
        }
    }

    assert((mpFreePos->__innerPrev == nullptr || mpFreePos->__innerPrev->inUse) &&
           (mpFreePos->__innerNext == nullptr || mpFreePos->__innerNext->inUse));
    // assert(healthCheck());
    assert(mTotalInUse >= 0);
}

bool DynamicBuffer::check()
{
    auto p = (BufBlk_t *)mBuffer;
    while (p)
    {
        if (!p->inUse &&
            p->__innerNext &&
            !p->__innerNext->inUse)
        {
            spdlog::error("[DynamicBuffer::healthCheck] fail: {}", dumpBlk(p));
            return false;
        }
        p = p->__innerNext;
    }
    return true;
}

} // namespace buffer
} // namespace mapper
