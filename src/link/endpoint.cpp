#include "endpoint.h"
#include <assert.h>
#include <fcntl.h>
#include <ifaddrs.h>
#include <netdb.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <sstream>
#include <spdlog/spdlog.h>

using namespace std;
using namespace mapper::buffer;

namespace mapper
{
namespace link
{

uint32_t Endpoint::gFreeCount = 0;
uint32_t Endpoint::gInUseCount = 0;

std::list<Endpoint_t *> Endpoint::gFreeList;

Endpoint_t *Endpoint::getEndpoint(Protocol_t protocol, Direction_t direction, Type_t type)
{
    if (gFreeList.empty())
    {
        batchAlloc(BATCH_ALLOC_COUNT);
    }

    Endpoint_t *pe = gFreeList.front();
    if (pe)
    {
        gFreeList.pop_front();

        --gFreeCount;
        ++gInUseCount;
        assert(gFreeCount >= 0);

        pe->init(protocol, direction, type);
    }

    return pe;
}

void Endpoint::releaseEndpoint(Endpoint_t *pe)
{
    assert(pe);

    gFreeList.push_front(pe);

    ++gFreeCount;
    --gInUseCount;
    assert(gInUseCount >= 0);

    if (gFreeCount > RELEASE_THRESHOLD)
    {
        for (auto i = BUFFER_SIZE; i < RELEASE_THRESHOLD; ++i)
        {
            auto pe = gFreeList.front();
            gFreeList.pop_front();
            delete pe;
            --gFreeCount;
        }

        assert(gFreeCount == BUFFER_SIZE);
    }
}

bool Endpoint::appendToSendList(Endpoint_t *pe, DynamicBuffer::BufBlk_t *pBufBlk)
{
    bool firstEntity = false;
    if (pe->sendListTail)
    {
        // 链表中有数据
        auto pTail = static_cast<DynamicBuffer::BufBlk_t *>(pe->sendListTail);

        pBufBlk->prev = pTail;
        pBufBlk->next = nullptr;

        pTail->next = pBufBlk;

        pe->sendListTail = pBufBlk;
    }
    else
    {
        // 当前链表为空
        pBufBlk->prev = pBufBlk->next = nullptr;
        pe->sendListHead = pe->sendListTail = pBufBlk;
        firstEntity = true;
    }

    // 更新总发送数
    assert(pBufBlk->sent == 0);
    pe->totalBufSize += pBufBlk->dataSize;

    return firstEntity;
}

uint32_t Endpoint::sendListLength(const Endpoint_t *pe)
{
    uint32_t length = 0;
    auto p = (DynamicBuffer::BufBlk_t *)pe->sendListHead;
    while (p)
    {
        ++length;
        p = p->next;
    }
    return length;
}

void Endpoint::batchAlloc(const uint32_t count)
{
    for (auto i = 0; i < count; ++i) {
        auto pe = new Endpoint_t;
        if (pe) {
            gFreeList.push_front(pe);
            ++gFreeCount;
        }
    }
}

} // namespace link
} // namespace mapper
