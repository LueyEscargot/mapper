#include "tunnel.h"
#include <assert.h>
#include <fcntl.h>
#include <arpa/inet.h>
#include <sys/epoll.h>
#include <spdlog/spdlog.h>
#include <sstream>

using namespace std;

namespace mapper
{
namespace link
{

uint32_t Tunnel::gFreeCount = 0;
uint32_t Tunnel::gInUseCount = 0;

std::list<Tunnel_t *> Tunnel::gFreeList;

Tunnel_t *Tunnel::getTunnel()
{
    if (gFreeList.empty())
    {
        batchAlloc(BATCH_ALLOC_COUNT);
    }

    Tunnel_t *pt = gFreeList.front();
    if (pt)
    {
        gFreeList.pop_front();

        --gFreeCount;
        ++gInUseCount;
        assert(gFreeCount >= 0);

        pt->init();
    }

    return pt;
}

void Tunnel::releaseTunnel(Tunnel_t *pt)
{
    assert(pt);

    gFreeList.push_front(pt);

    ++gFreeCount;
    --gInUseCount;
    assert(gInUseCount >= 0);

    if (gFreeCount > RELEASE_THRESHOLD)
    {
        for (auto i = BUFFER_SIZE; i < RELEASE_THRESHOLD; ++i)
        {
            auto pt = gFreeList.front();
            gFreeList.pop_front();
            delete pt;
            --gFreeCount;
        }

        assert(gFreeCount == BUFFER_SIZE);
    }
}

void Tunnel::batchAlloc(const uint32_t count)
{
    for (auto i = 0; i < count; ++i)
    {
        auto pt = new Tunnel_t;
        if (pt)
        {
            gFreeList.push_front(pt);
            ++gFreeCount;
        }
    }
}

} // namespace link
} // namespace mapper
