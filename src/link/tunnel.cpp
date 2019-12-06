#include "tunnel.h"
#include <fcntl.h>
#include <arpa/inet.h>
#include <spdlog/spdlog.h>

using namespace std;

namespace mapper
{
namespace link
{

/**
 * tunnel state machine:
 * 
 *       CLOSED -----> ALLOCATED -----> INITIALIZED -----> CONNECT -----> ESTABLISHED
 *         A                                 |                |               |
 *         |                                 |                |               |
 *         |                                 *--------------->|<--------------*
 *         |                                                  |
 *         |                                                  V
 *         *---------------------------------------------- BROKEN
 */
const bool Tunnel::StateMaine[TUNNEL_STATE_COUNT][TUNNEL_STATE_COUNT] = {
    // CLOSED | ALLOCATED | INITIALIZED | CONNECT | ESTABLISHED | BROKEN
    {true, true, false, false, false, false},  // CLOSED
    {false, true, true, false, false, false},  // ALLOCATED
    {false, false, true, true, false, true},   // INITIALIZED
    {false, false, false, true, true, true},   // CONNECT
    {false, false, false, false, true, true},  // ESTABLISHED
    {true, false, false, false, false, true},  //  BROKEN
};

bool Tunnel::init(Tunnel_t *pt, EndpointService_t *pes, int southSoc)
{
    assert(pt->status == TunnelState_t::ALLOCATED);

    pt->setAddrInfo(pes->targetHostAddrs);

    // create to north socket
    int northSoc;
    while (pt->curAddr)
    {
        // create to north socket
        northSoc = socket(pt->curAddr->ai_family, pt->curAddr->ai_socktype, pt->curAddr->ai_protocol);
        if (northSoc <= 0)
        {
            spdlog::error("[Tunnel::init] create socket fail{}.: {} - {}",
                          pt->curAddr->ai_next ? " try again" : "", errno, strerror(errno));
            pt->curAddr = pt->curAddr->ai_next;
            continue;
        }

        // set socket to non-blocking mode
        if (fcntl(northSoc, F_SETFL, O_NONBLOCK) < 0)
        {
            spdlog::error("[Tunnel::init] set socket to non-blocking mode fail{}. {}: {}",
                          pt->curAddr->ai_next ? " try again" : "", errno, strerror(errno));
            ::close(northSoc);
            northSoc = 0;
            pt->curAddr = pt->curAddr->ai_next;
            continue;
        }

        break;
    }
    if (northSoc <= 0)
    {
        spdlog::error("[Tunnel::init] create to north socket fail");
        return false;
    }

    // set north soc and south soc for tunnel
    pt->init(southSoc, northSoc);
    Tunnel::setStatus(pt, TunnelState_t::INITIALIZED);

    return true;
}

void Tunnel::setStatus(Tunnel_t *pt, TunnelState_t stat)
{
    if (!StateMaine[pt->status][stat])
    {
        for (int x = 0; x < TUNNEL_STATE_COUNT; ++x)
        {
            for (int y = 0; y < TUNNEL_STATE_COUNT; ++y)
            {
                printf("\t%s", StateMaine[x][y] ? "true" : "false");
            }
            printf("\n");
        }

        spdlog::critical("[Tunnel::setSTatus] invalid status convert: {} --> {}.", pt->status, stat);
        assert(false);
    }

    pt->status = stat;
}

bool Tunnel::connect(Tunnel_t *pt)
{
    switch (pt->status)
    {
    case TunnelState_t::INITIALIZED:
        setStatus(pt, TunnelState_t::CONNECT);
        break;
    case TunnelState_t::CONNECT:
        break;
    default:
        spdlog::critical("[Tunnel::connect] invalid tunnel status.", pt->status);
        assert(false);
    }

    if ([&]() -> bool {
            if (pt->curAddr == nullptr)
            {
                spdlog::debug("[Tunnel::connect] no more retry address");
                return false;
            }

            for (char ip[INET_ADDRSTRLEN]; pt->curAddr; pt->curAddr = pt->curAddr->ai_next)
            {
                sockaddr_in *sai = reinterpret_cast<sockaddr_in *>(pt->curAddr->ai_addr);
                inet_ntop(AF_INET, &sai->sin_addr, ip, pt->curAddr->ai_addrlen);

                spdlog::debug("[Tunnel::connect] soc[{}] connect to {}:{}",
                              pt->north.soc, ip, ntohs(sai->sin_port));

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
                        spdlog::error("[Tunnel::connect] connect fail: {}, {}", errno, strerror(errno));
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

bool Tunnel::onSoc(uint64_t curTime, EndpointRemote_t *per, uint32_t events)
{
    spdlog::critical("[Tunnel::onSoc] NOT Implemented yet.");

    Tunnel_t *pt = static_cast<Tunnel_t *>(per->tunnel);

    // set status as broken
    setStatus(pt, TunnelState_t::BROKEN);

    return false;
}

} // namespace link
} // namespace mapper
