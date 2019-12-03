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
    : mpTunnels(nullptr)
{
}

TunnelMgr::~TunnelMgr()
{
    close();
}

bool TunnelMgr::init(uint32_t maxTunnels)
{
    spdlog::debug("[TunnelMgr::init] init for {} blocks.");

    if (mpTunnels)
    {
        spdlog::error("[TunnelMgr::init] instance initialized already.");
        return false;
    }

    mpTunnels = static_cast<Tunnel_t *>(malloc(sizeof(Tunnel_t) * maxTunnels));
    if (!mpTunnels)
    {
        spdlog::error("[TunnelMgr::init] create block array fail.");
        return false;
    }

    for (int i = 0; i < maxTunnels; ++i)
    {
        mFreeList.push_back(mpTunnels + i);
    }
}

void TunnelMgr::close()
{
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

bool TunnelMgr::connectToTarget(Tunnel_t *pt)
{
    assert(pt->status == TunnelState_t::INITIALIZED);

    if ([&]() -> bool {
            if (pt->curAddr == nullptr)
            {
                spdlog::debug("[Tunnel::connectToTarget] no more retry address");
                return false;
            }

            for (char ip[INET_ADDRSTRLEN]; pt->curAddr; pt->curAddr = pt->curAddr->ai_next)
            {
                inet_ntop(AF_INET, &pt->curAddr->ai_addr, ip, INET_ADDRSTRLEN);
                spdlog::debug("[Tunnel::connectToTarget] soc[{}] connect to {}", ip);

                // connect to host
                if (connect(pt->north.soc, pt->curAddr->ai_addr, pt->curAddr->ai_addrlen) < 0 &&
                    errno != EALREADY && errno != EINPROGRESS)
                {
                    if (pt->curAddr->ai_next)
                    {
                        spdlog::error("[Tunnel::connectToTarget] connect fail: {}, {}, try again",
                                      errno, strerror(errno));
                    }
                    else
                    {
                        spdlog::error("[Tunnel::connectToTarget] connect fail: {}, {}",
                                      errno, strerror(errno));
                        ::close(pt->north.soc);
                        return false;
                    }
                }
                else
                {
                    return true;
                }
            }

            spdlog::debug("[Tunnel::connectToTarget] north sock[{}] connect fail.", pt->north.soc);
            return false;
        }())
    {
        pt->status == TunnelState_t::BROKEN;
        return false;
    }
    else
    {
        return true;
    }
}

bool TunnelMgr::init(const EndpointService_t *pes, int clientSoc, Tunnel_t *pt)
{
    // create to north socket
    int soc = 0;
    if ((soc = socket(pt->curAddr->ai_family,
                      pt->curAddr->ai_socktype,
                      pt->curAddr->ai_protocol)) < 0)
    {
        spdlog::error("[Tunnel::init] create socket fail: {} - {}",
                      errno, strerror(errno));
        return false;
    }

    // set socket to non-blocking mode
    if (fcntl(soc, F_SETFL, O_NONBLOCK) < 0)
    {
        spdlog::error("[Tunnel::init] set socket to non-blocking mode fail. {}: {}",
                      errno, strerror(errno));
        ::close(soc);
        return false;
    }

    // init new allocated tunnel object
    pt->initAsConnect(soc, clientSoc);
    return true;
}

} // namespace link
} // namespace mapper
