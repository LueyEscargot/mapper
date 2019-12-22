#include "tunnel.h"
#include <fcntl.h>
#include <arpa/inet.h>
#include <sys/epoll.h>
#include <spdlog/spdlog.h>
#include <sstream>
#include "endpoint.h"

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
    {1, 1, 0, 0, 0, 0}, // CLOSED
    {0, 1, 1, 0, 0, 0}, // ALLOCATED
    {0, 0, 1, 1, 0, 1}, // INITIALIZED
    {0, 0, 0, 1, 1, 1}, // CONNECT
    {0, 0, 0, 0, 1, 1}, // ESTABLISHED
    {1, 0, 0, 0, 0, 1}, // BROKEN
};

bool Tunnel::init(Tunnel_t *pt, EndpointService_t *pes, int southSoc)
{
    assert(pt->status == TunnelState_t::ALLOCATED);

    pt->setAddrInfo(pes->targetHostAddrs);

    if (pes->protocol == link::Protocol_t::TCP)
    {
        // create to north socket
        int northSoc = 0;
        while (pt->curAddr)
        {
            // create to north socket
            northSoc = socket(pt->curAddr->ai_family, pt->curAddr->ai_socktype, pt->curAddr->ai_protocol);
            if (northSoc <= 0)
            {
                spdlog::error("[Tunnel::init] create socket fail{}.: {} - {}",
                              pt->curAddr->ai_next ? " try again" : "", errno, strerror(errno));
            }
            // set socket to non-blocking mode
            else if (fcntl(northSoc, F_SETFL, O_NONBLOCK) < 0)
            {
                spdlog::error("[Tunnel::init] set socket to non-blocking mode fail{}. {}: {}",
                              pt->curAddr->ai_next ? " try again" : "", errno, strerror(errno));
                ::close(northSoc);
                northSoc = 0;
            }
            else
            {
                break;
            }

            pt->curAddr = pt->curAddr->ai_next;
        }
        if (northSoc <= 0)
        {
            spdlog::error("[Tunnel::init] create to north socket fail");
            return false;
        }

        // set north soc and south soc for tunnel
        pt->init(link::Protocol_t::TCP, southSoc, northSoc);
        Tunnel::setStatus(pt, TunnelState_t::INITIALIZED);

        return true;
    }
    else
    {
        spdlog::error("[Tunnel::init] Routine for UDP Not Implemeted yet.");
        return false;
    }
}

void Tunnel::setStatus(Tunnel_t *pt, TunnelState_t stat)
{
    if (!StateMaine[pt->status][stat])
    {
        // for (int x = 0; x < TUNNEL_STATE_COUNT; ++x)
        // {
        //     for (int y = 0; y < TUNNEL_STATE_COUNT; ++y)
        //     {
        //         printf("\t%s", StateMaine[x][y] ? "true" : "false");
        //     }
        //     printf("\n");
        // }

        spdlog::critical("[Tunnel::setSTatus] invalid status convert: {} --> {}.", pt->status, stat);
        assert(false);
    }

    pt->status = stat;
}

string Tunnel::toStr(Tunnel_t *pt)
{
    stringstream ss;

    ss << "["
       << pt->south.soc << ","
       << pt->north.soc
       << "]";

    return ss.str();
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

            spdlog::debug("[Tunnel::connect] north soc[{}] connect fail.", pt->north.soc);
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

bool Tunnel::onSoc(uint64_t curTime,
                   EndpointRemote_t *per,
                   uint32_t events,
                   CB_SetEpollMode cbSetEpollMode,
                   CB_onEstablish cbOnEstablish)
{
    // Tunnel_t *pt = static_cast<Tunnel_t *>(per->tunnel);

    // // TCP or UDP
    // if (pt->protocol == Protocol_t::TCP)
    // {
    //     assert(per->type & (Type_t::NORTH | Type_t::SOUTH));

    //     bool result = false;

    //     // spdlog::debug("[Tunnel::onSoc] curTime[{}], endpoint[{}], event.events[0x{:x}]",
    //     //               curTime, Endpoint::toStr(per), events);

    //     // send
    //     if (events & EPOLLOUT)
    //     {
    //         if (per->type == Type_t::NORTH)
    //         {
    //             result = northSocSend(pt, cbSetEpollMode, cbOnEstablish);
    //         }
    //         else
    //         {
    //             result = southSocSend(pt, cbSetEpollMode);
    //         }
    //     }

    //     // recv
    //     if (events & EPOLLIN)
    //     {
    //         if (per->type == Type_t::NORTH)
    //         {
    //             result = northSocRecv(pt, cbSetEpollMode);
    //         }
    //         else
    //         {
    //             result = southSocRecv(pt, cbSetEpollMode);
    //         }
    //     }

    //     if (result)
    //     {
    //         return true;
    //     }
    //     else
    //     {
    //         // current endpoint not valid
    //         per->valid = false;

    //         // set tunnel as broken
    //         setStatus(pt, TunnelState_t::BROKEN);
    //         return false;
    //     }
    // }
    // else
    // {
    //     // TODO: finish UDP routine
    //     spdlog::critical("[Tunnel::onSoc] Routine for UDP Not Implemeted yet.");
    //     return false;
    // }
}

bool Tunnel::northSocRecv(Tunnel_t *pt)
{
    while (true)
    {
        uint64_t bufSize = pt->toSouthBUffer->freeSize();

        // spdlog::trace("[Tunnel::northSocRecv] bufSize: {}, pt->toSouthBUffer: {}",
        //               bufSize, pt->toSouthBUffer->toStr());

        if (bufSize == 0)
        {
            // 接收缓冲区已满
            pt->toSouthBUffer->stopRecv = true;
            break;
        }

        char *buf = pt->toSouthBUffer->getBuffer();
        int nRet = recv(pt->north.soc, buf, bufSize, 0);
        if (nRet <= 0)
        {
            if (errno == EAGAIN)
            {
                // 此次数据接收已完毕
                break;
            }
            else
            {
                if (nRet == 0)
                {
                    // soc closed by peer
                    spdlog::debug("[Tunnel::northSocRecv] soc[{}] closed by north peer", pt->north.soc);
                }
                else
                {
                    spdlog::error("[Tunnel::northSocRecv] host soc[{}] recv fail: {}:[]",
                                  pt->north.soc, errno, strerror(errno));
                }

                spdlog::trace("[Tunnel::northSocRecv] current stat: bufSize: {}, pt->toNorthBUffer: {}",
                              bufSize, pt->toSouthBUffer->toStr());

                pt->north.valid = false;

                return false;
            }
        }
        else
        {
            pt->toSouthBUffer->incDataSize(nRet);
        }
    }

    return true;
}

bool Tunnel::northSocSend(Tunnel_t *pt)
{
    while (uint64_t bufSize = pt->toNorthBUffer->dataSize())
    {
        // send data to north
        char *buf = pt->toNorthBUffer->getData();
        int nRet = send(pt->north.soc, buf, bufSize, 0);
        if (nRet < 0)
        {
            if (errno == EAGAIN)
            {
                // 此次发送窗口已关闭
                break;
            }

            spdlog::debug("[Tunnel::northSocSend] soc[{}] send fail: {} - [{}]",
                          pt->north.soc, errno, strerror(errno));

            pt->north.valid = false;

            return false;
        }

        pt->toNorthBUffer->incFreeSize(nRet);
    }

    return true;
}

bool Tunnel::southSocRecv(Tunnel_t *pt)
{
    while (true)
    {
        uint64_t bufSize = pt->toNorthBUffer->freeSize();

        // spdlog::trace("[Tunnel::southSocRecv] bufSize: {}, pt->toNorthBUffer: {}",
        //               bufSize, pt->toNorthBUffer->toStr());

        if (bufSize == 0)
        {
            // 接收缓冲区已满
            pt->toNorthBUffer->stopRecv = true;
            break;
        }

        char *buf = pt->toNorthBUffer->getBuffer();
        int nRet = recv(pt->south.soc, buf, bufSize, 0);
        if (nRet <= 0)
        {
            if (errno == EAGAIN)
            {
                // 此次数据接收已完毕
                break;
            }
            else
            {
                if (nRet == 0)
                {
                    // soc closed by peer
                    spdlog::debug("[Tunnel::southSocRecv] soc[{}] closed by south peer", pt->south.soc);
                }
                else
                {
                    spdlog::error("[Tunnel::southSocRecv] south soc[{}] recv fail: {}:[]",
                                  pt->south.soc, errno, strerror(errno));
                }

                spdlog::trace("[Tunnel::southSocRecv] current stat: bufSize: {}, pt->toNorthBUffer: {}",
                              bufSize, pt->toNorthBUffer->toStr());

                pt->south.valid = false;

                return false;
            }
        }
        else
        {
            pt->toNorthBUffer->incDataSize(nRet);
        }
    }

    return true;
}

bool Tunnel::southSocSend(Tunnel_t *pt)
{
    while (uint64_t bufSize = pt->toSouthBUffer->dataSize())
    {
        // send data to south
        char *buf = pt->toSouthBUffer->getData();
        int nRet = send(pt->south.soc, buf, bufSize, 0);
        if (nRet < 0)
        {
            if (errno == EAGAIN)
            {
                // 此次发送窗口已关闭
                break;
            }

            spdlog::debug("[Tunnel::southSocSend] soc[{}] send fail: {} - [{}]",
                          pt->south.soc, errno, strerror(errno));
            pt->south.valid = false;

            return false;
        }

        pt->toSouthBUffer->incFreeSize(nRet);
    }

    return true;
}

} // namespace link
} // namespace mapper
