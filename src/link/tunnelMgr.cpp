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
    close();
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
        mpTunnels[i].init(pCfg->getLinkSouthBuf(), pCfg->getLinkNorthBuf());
        mFreeList.push_back(mpTunnels + i);
    }
}

void TunnelMgr::close()
{
    spdlog::debug("[TunnelMgr::close] release tunnel array and send/recv buffer in each tunnel.");
    if (mpTunnels)
    {
        // release send/recv buffers
        for (int i = 0; i < mTunnelCounts; ++i)
        {
            mpTunnels[i].close();
        }

        // release tunnel array
        free(mpTunnels);
        mpTunnels = nullptr;
        mTunnelCounts = 0;
    }
}

Tunnel_t *TunnelMgr::allocTunnel(EndpointService_t *pes)
{
    if (mFreeList.empty())
    {
        return nullptr;
    }

    Tunnel_t *pt = mFreeList.front();
    mFreeList.pop_front();

    pt->init(pes->targetHostAddrs);

    return pt;
}

void TunnelMgr::freeTunnel(Tunnel_t *pt)
{
    spdlog::error("[TunnelMgr::freeTunnel] not implimented yet.");
    if (pt->status != TunnelState_t::BROKEN)
    {
        spdlog::warn("[Tunnel::freeTunnel] invalid status: {}", pt->status);
    }

    // close tunnel
    pt->close();

    mFreeList.push_front(pt);
}

bool TunnelMgr::connect(Tunnel_t *pt)
{
    assert(pt->status == TunnelState_t::CONNECT);

    if ([&]() -> bool {
            if (pt->curAddr == nullptr)
            {
                spdlog::debug("[Tunnel::connect] no more retry address");
                return false;
            }

            for (char ip[INET_ADDRSTRLEN]; pt->curAddr; pt->curAddr = pt->curAddr->ai_next)
            {
                inet_ntop(AF_INET, &(pt->curAddr->ai_addr), ip, INET_ADDRSTRLEN);
                spdlog::debug("[Tunnel::connect] soc[{}] connect to {}", pt->north.soc, ip);

                // connect to host
                if (::connect(pt->north.soc, pt->curAddr->ai_addr, pt->curAddr->ai_addrlen) < 0 &&
                    errno != EALREADY && errno != EINPROGRESS)
                {
                    if (pt->curAddr->ai_next)
                    {
                        spdlog::error("[Tunnel::connect] connect fail: {}, {}, try again",
                                      errno, strerror(errno));
                    }
                    else
                    {
                        spdlog::error("[Tunnel::connect] connect fail: {}, {}",
                                      errno, strerror(errno));
                        ::close(pt->north.soc);
                        pt->north.valid = false;
                        return false;
                    }
                }
                else
                {
                    return true;
                }
            }

            spdlog::debug("[Tunnel::connect] north sock[{}] connect fail.", pt->north.soc);
            return false;
        }())
    {
        return true;
    }
    else
    {
        pt->status == TunnelState_t::BROKEN;
        return false;
    }
}

bool TunnelMgr::onSoc(uint64_t curTime, EndpointRemote_t *per, uint32_t events)
{
    spdlog::critical("[Tunnel::onSoc] NOT Implemented yet.");
    return false;
}

bool TunnelMgr::init(Tunnel_t *pet, int southSoc)
{
    // create to north socket
    int northSoc;
    if ((northSoc = socket(pet->curAddr->ai_family,
                           pet->curAddr->ai_socktype,
                           pet->curAddr->ai_protocol)) < 0)
    {
        spdlog::error("[Tunnel::init] create socket fail: {} - {}",
                      errno, strerror(errno));
        return false;
    }

    // set socket to non-blocking mode
    if (fcntl(northSoc, F_SETFL, O_NONBLOCK) < 0)
    {
        spdlog::error("[Tunnel::init] set socket to non-blocking mode fail. {}: {}",
                      errno, strerror(errno));
        ::close(northSoc);
        return false;
    }

    // init new allocated tunnel object
    pet->setAsConnect(southSoc, northSoc);
    return true;
}

} // namespace link
} // namespace mapper
