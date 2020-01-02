#include "tunnelMgr.h"
#include <assert.h>
#include <fcntl.h>
#include <ifaddrs.h>
#include <netdb.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <sstream>
#include <spdlog/spdlog.h>
#include "endpoint.h"
#include "tunnel.h"
#include "../buffer/buffer.h"

using namespace std;

namespace mapper
{
namespace link
{

TunnelMgr::TunnelMgr()
    : mpTunnels(nullptr),
      mTunnelCounts(0)
{
}

TunnelMgr::~TunnelMgr()
{
}

bool TunnelMgr::init(config::Config *pCfg)
{
    assert(pCfg);

    if (mpTunnels)
    {
        spdlog::error("[TunnelMgr::init] instance initialized already.");
        return false;
    }

    mTunnelCounts = pCfg->getLinkTunnels();
    spdlog::debug("[TunnelMgr::init] init for {} blocks.", mTunnelCounts);

    mpTunnels = static_cast<Tunnel_t *>(malloc(sizeof(Tunnel_t) * mTunnelCounts));
    if (!mpTunnels)
    {
        spdlog::error("[TunnelMgr::init] create block array fail.");
        return false;
    }

    for (int i = 0; i < mTunnelCounts; ++i)
    {
        uint32_t northBufSize = pCfg->getLinkSouthBuf();
        uint32_t southBufSize = pCfg->getLinkNorthBuf();
        buffer::Buffer *northBuffer = buffer::Buffer::alloc(northBufSize);
        buffer::Buffer *southBuffer = buffer::Buffer::alloc(southBufSize);
        if (northBuffer && southBuffer)
        {
            mpTunnels[i].create(northBuffer, southBuffer);
            mFreeList.push_back(mpTunnels + i);
        }
        else
        {
            spdlog::error("[TunnelMgr::init] alloc tunnel buffer fail.");
            buffer::Buffer::release(northBuffer);
            buffer::Buffer::release(southBuffer);
            return false;
        }
    }

    return true;
}

void TunnelMgr::close()
{
    spdlog::debug("[TunnelMgr::close] release tunnel array and send/recv buffer in each tunnel.");
    if (mpTunnels)
    {
        // release send/recv buffers
        for (int i = 0; i < mTunnelCounts; ++i)
        {
            buffer::Buffer::release(mpTunnels[i].toNorthBuffer);
            buffer::Buffer::release(mpTunnels[i].toSouthBuffer);
            mpTunnels[i].toNorthBuffer = nullptr;
            mpTunnels[i].toSouthBuffer = nullptr;
            mpTunnels[i].close();
        }

        // release tunnel array
        free(mpTunnels);
        mpTunnels = nullptr;
        mTunnelCounts = 0;
    }
    spdlog::debug("[TunnelMgr::close] closed.");
}

Tunnel_t *TunnelMgr::allocTunnel()
{
    if (mFreeList.empty())
    {
        return nullptr;
    }

    Tunnel_t *pt = mFreeList.front();
    mFreeList.pop_front();

    Tunnel::setStatus(pt, TunnelState_t::ALLOCATED);

    return pt;
}

void TunnelMgr::freeTunnel(Tunnel_t *pt)
{
    if (pt->status != TunnelState_t::BROKEN)
    {
        spdlog::critical("[Tunnel::freeTunnel] invalid status: {}", pt->status);
        assert(false);
    }

    // close tunnel
    pt->close();
    Tunnel::setStatus(pt, TunnelState_t::CLOSED);

    mFreeList.push_front(pt);
}

} // namespace link
} // namespace mapper
