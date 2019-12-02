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

Tunnel_t *TunnelMgr::allocTunnel(Protocol_t protocol,
                                 const char *intf,
                                 const char *service,
                                 const char *targetHost,
                                 const char *targetService)
{
    if (mFreeList.empty())
    {
        return nullptr;
    }

    Tunnel_t *pTunnel = mFreeList.front();
    mFreeList.pop_front();

    // // get addr info
    // if (!getAddrInfo(pes, pet) || pet->addrHead == nullptr)
    // {
    //     spdlog::error("[Tunnel::allocTunnel] get address of target[{}] fail", pes->targetHost);
    //     return false;
    // }

    // // create socket and connect to target host
    // if (!connectToTarget(pes, pet) || pet->addrHead == nullptr)
    // {
    //     spdlog::error("[Tunnel::allocTunnel] connect to target[{}] fail", pes->targetHost);
    //     return false;
    // }

    pTunnel->status = TunnelState_t::INITIALIZED;

    return pTunnel;
}

void TunnelMgr::freeTunnel(Tunnel_t *pTunnel)
{
    // TODO: implement this function.
    spdlog::error("[TunnelMgr::freeTunnel] not implimented yet.");
}

string TunnelMgr::toStr(const Tunnel_t *pTunnel)
{
    stringstream ss;

    ss << "["
       << Endpoint::toStr(&pTunnel->south) << ","
       << Endpoint::toStr(&pTunnel->north) << ","
       << pTunnel->tag
       << "]";

    return ss.str();
}

bool TunnelMgr::getAddrInfo(const EndpointService_t *pes, Tunnel_t *pet)
{
    addrinfo hints;

    // init hints
    memset(&hints, 0, sizeof(struct addrinfo));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = 0;
    hints.ai_protocol = IPPROTO_TCP;
    hints.ai_canonname = nullptr;
    hints.ai_addr = nullptr;
    hints.ai_next = nullptr;

    int nRet = getaddrinfo(pes->targetHost, pes->targetService, &hints, &pet->addrHead);
    if (nRet != 0)
    {
        spdlog::error("[Tunnel::getAddrInfo] getaddrinfo fail: {}", gai_strerror(nRet));
        return false;
    }
}

bool TunnelMgr::connectToTarget(const EndpointService_t *pes, Tunnel_t *pet)
{
    if ((pet->north.soc = socket(pet->curAddr->ai_family,
                                 pet->curAddr->ai_socktype,
                                 pet->curAddr->ai_protocol)) < 0)
    {
        spdlog::error("[Tunnel::connectToTarget] socket creation error{}: {}",
                      errno, strerror(errno));
        return false;
    }

    // set socket to non-blocking mode
    if (fcntl(pet->north.soc, F_SETFL, O_NONBLOCK) < 0)
    {
        spdlog::error("[Tunnel::connectToTarget] set socket to non-blocking mode fail. {}: {}",
                      errno, strerror(errno));
        ::close(pet->north.soc);
        pet->north.soc = 0;
        return false;
    }

    char ip[INET_ADDRSTRLEN];

    for (pet->curAddr = pet->addrHead; pet->curAddr; pet->curAddr = pet->curAddr->ai_next)
    {
        inet_ntop(AF_INET, &pet->curAddr->ai_addr, ip, INET_ADDRSTRLEN);
        spdlog::debug("[Tunnel::connectToTarget] connect to {} ({}:{})",
                      pes->targetHost, ip, pes->targetService);

        // connect to host
        if (connect(pet->north.soc, pet->curAddr->ai_addr, pet->curAddr->ai_addrlen) < 0 &&
            errno != EALREADY && errno != EINPROGRESS)
        {
            if (pet->curAddr->ai_next)
            {
                spdlog::error("[Tunnel::connectToTarget] connect fail: {}, {}, try again",
                              errno, strerror(errno));
            }
            else
            {
                spdlog::error("[Tunnel::connectToTarget] connect fail: {}, {}",
                              errno, strerror(errno));
                ::close(pet->north.soc);
                pet->north.soc = 0;
                return false;
            }
        }
        else
        {
            return true;
        }
    }
}

} // namespace link
} // namespace mapper
