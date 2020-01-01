#include "udpTunnel.h"
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
 * UDP tunnel state machine:
 * 
 *                                           *--------------------------------*
 *                                           |                                |
 *                                           |                                V
 *       CLOSED -----> ALLOCATED -----> INITIALIZED -----> CONNECT -----> ESTABLISHED
 *         A                                 |                |               |
 *         |                                 |                |               |
 *         |                                 *--------------->|<--------------*
 *         |                                                  |
 *         |                                                  V
 *         *---------------------------------------------- BROKEN
 */
const bool UdpTunnel::StateMaine[TUNNEL_STATE_COUNT][TUNNEL_STATE_COUNT] = {
    // CLOSED | ALLOCATED | INITIALIZED | CONNECT | ESTABLISHED | BROKEN
    {1, 1, 0, 0, 0, 0}, // CLOSED
    {0, 1, 1, 0, 0, 0}, // ALLOCATED
    {0, 0, 1, 1, 1, 1}, // INITIALIZED
    {0, 0, 0, 1, 1, 1}, // CONNECT
    {0, 0, 0, 0, 1, 1}, // ESTABLISHED
    {1, 0, 0, 0, 0, 1}, // BROKEN
};

bool UdpTunnel::init(UdpTunnel_t *pt,
                     EndpointService_t *pes,
                     sockaddr_storage *south,
                     socklen_t south_len,
                     buffer::DynamicBuffer::BufBlk_t *toNorthBuf)
{
    assert(pt->status == TunnelState_t::ALLOCATED);

    // pt->setAddrInfo(pes->targetHostAddrs);

    // if (pes->protocol == link::Protocol_t::TCP)
    // {
    //     // create to north socket
    //     int northSoc = 0;
    //     while (pt->curAddr)
    //     {
    //         // create to north socket
    //         northSoc = socket(pt->curAddr->ai_family, pt->curAddr->ai_socktype, pt->curAddr->ai_protocol);
    //         if (northSoc <= 0)
    //         {
    //             spdlog::error("[UdpTunnel::init] create socket fail{}.: {} - {}",
    //                           pt->curAddr->ai_next ? " try again" : "", errno, strerror(errno));
    //         }
    //         // set socket to non-blocking mode
    //         else if (fcntl(northSoc, F_SETFL, O_NONBLOCK) < 0)
    //         {
    //             spdlog::error("[UdpTunnel::init] set socket to non-blocking mode fail{}. {}: {}",
    //                           pt->curAddr->ai_next ? " try again" : "", errno, strerror(errno));
    //             ::close(northSoc);
    //             northSoc = 0;
    //         }
    //         else
    //         {
    //             break;
    //         }

    //         pt->curAddr = pt->curAddr->ai_next;
    //     }
    //     if (northSoc <= 0)
    //     {
    //         spdlog::error("[UdpTunnel::init] create to north socket fail");
    //         return false;
    //     }

    //     // set north soc and south soc for tunnel
    //     pt->init(pes, south, south_len, toNorthBuf);
    //     UdpTunnel::setStatus(pt, TunnelState_t::INITIALIZED);

    //     return true;
    // }
    // else
    // {
    //     spdlog::error("[UdpTunnel::init] Routine for UDP Not Implemeted yet.");
    //     return false;
    // }
}

void UdpTunnel::setStatus(UdpTunnel_t *pt, TunnelState_t stat)
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

        spdlog::critical("[UdpTunnel::setSTatus] invalid status convert: {} --> {}.", pt->status, stat);
        assert(false);
    }

    pt->status = stat;
}

string UdpTunnel::toStr(UdpTunnel_t *pt)
{
    stringstream ss;

    ss << "["
       // TODO: implement this
       //    << pt->south.soc << ","
       //    << pt->north.soc
       << "]";

    return ss.str();
}

bool UdpTunnel::connect(UdpTunnel_t *pt)
{
    switch (pt->status)
    {
    case TunnelState_t::INITIALIZED:
        setStatus(pt, TunnelState_t::CONNECT);
        break;
    case TunnelState_t::CONNECT:
        break;
    default:
        spdlog::critical("[UdpTunnel::connect] invalid tunnel status.", pt->status);
        assert(false);
    }

    // if ([&]() -> bool {
    //         if (pt->curAddr == nullptr)
    //         {
    //             spdlog::debug("[UdpTunnel::connect] no more retry address");
    //             return false;
    //         }

    //         for (char ip[INET_ADDRSTRLEN]; pt->curAddr; pt->curAddr = pt->curAddr->ai_next)
    //         {
    //             sockaddr_in *sai = reinterpret_cast<sockaddr_in *>(pt->curAddr->ai_addr);
    //             inet_ntop(AF_INET, &sai->sin_addr, ip, pt->curAddr->ai_addrlen);

    //             spdlog::debug("[UdpTunnel::connect] soc[{}] connect to {}:{}",
    //                           pt->north.soc, ip, ntohs(sai->sin_port));

    //             // connect to host
    //             if (::connect(pt->north.soc, pt->curAddr->ai_addr, pt->curAddr->ai_addrlen) < 0 &&
    //                 errno != EALREADY && errno != EINPROGRESS)
    //             {
    //                 if (pt->curAddr->ai_next)
    //                 {
    //                     spdlog::error("[UdpTunnel::connect] connect fail: {}, {}, try again",
    //                                   errno, strerror(errno));
    //                 }
    //                 else
    //                 {
    //                     spdlog::error("[UdpTunnel::connect] connect fail: {}, {}", errno, strerror(errno));
    //                     ::close(pt->north.soc);
    //                     pt->north.valid = false;
    //                     return false;
    //                 }
    //             }
    //             else
    //             {
    //                 return true;
    //             }
    //         }

    //         spdlog::debug("[UdpTunnel::connect] north soc[{}] connect fail.", pt->north.soc);
    //         return false;
    //     }())
    // {
    //     return true;
    // }
    // else
    // {
    //     pt->status == TunnelState_t::BROKEN;
    //     return false;
    // }
}

bool UdpTunnel::northSocRecv(UdpTunnel_t *pt)
{
    // buffer::DynamicBuffer *pDynBuf = pt->service->dynamicBuffer;

    // while (true)
    // {
    //     void *pBuf = pDynBuf->reserve(MAX_RESERVE_SIZE);
    //     if (pBuf == nullptr)
    //     {
    //         // 已无足够大的空闲接收缓冲区
    //         pt->toSouthBufStopRecv = true;
    //         break;
    //     }

    //     sockaddr_in sin;
    //     socklen_t sl = sizeof(sockaddr_in);
    //     int nRet = recvfrom(pt->north.soc, pBuf, MAX_RESERVE_SIZE, 0, (sockaddr *)&sin, &sl);
    //     if (nRet > 0)
    //     {
    //         buffer::DynamicBuffer::BufBlk_t *pBufBlk = pDynBuf->cut(nRet);
    //         // append to tail of send list
    //         appendToSendList(pt->service->toSouthBufList, pBufBlk);
    //     }
    //     else if (nRet < 0)
    //     {
    //         if (errno == EAGAIN)
    //         {
    //             // 此次数据接收已完毕
    //             break;
    //         }
    //         else
    //         {
    //             spdlog::error("[UdpTunnel::northSocRecv] north soc[{}] recv fail: {}:[]",
    //                           pt->north.soc, errno, strerror(errno));

    //             pt->north.valid = false;

    //             return false;
    //         }
    //     }
    //     else
    //     {
    //         spdlog::trace("[UdpTunnel::northSocRecv] recv empty packet from north soc[{}]", pt->north.soc);
    //     }
    // }

    return true;
}

bool UdpTunnel::northSocSend(UdpTunnel_t *pt)
{
    // buffer::DynamicBuffer *pDynBuf = pt->service->dynamicBuffer;
    // auto p = pt->service->toSouthBufList.next;

    // while (p)
    // {


    //     void *pBuf = pt->service->toSouthBufList.next->buffer;
    //     // send data to north
    //     // int nRet = sendto(pt->north.soc,
    //     //                 pt->toSouthBufList.next->buffer,
    //     //                 pt->toSouthBufList.next->size,
    //     //                 0);
    //     // if (nRet < 0)
    //     // {
    //     //     if (errno == EAGAIN)
    //     //     {
    //     //         // 此次发送窗口已关闭
    //     //         break;
    //     //     }

    //     //     spdlog::debug("[UdpTunnel::northSocSend] soc[{}] send fail: {} - [{}]",
    //     //                   pt->north.soc, errno, strerror(errno));

    //     //     pt->north.valid = false;

    //     //     return false;
    //     // }

    //     // pt->toNorthBuffer->incFreeSize(nRet);
    // }

    return true;
}

bool UdpTunnel::southSocRecv(UdpTunnel_t *pt)
{
    // while (true)
    // {
    //     uint64_t bufSize = pt->toNorthBuffer->freeSize();

    //     // spdlog::trace("[UdpTunnel::southSocRecv] bufSize: {}, pt->toNorthBuffer: {}",
    //     //               bufSize, pt->toNorthBuffer->toStr());

    //     if (bufSize == 0)
    //     {
    //         // 已无足够大的空闲接收缓冲区
    //         pt->toNorthBuffer->stopRecv = true;
    //         break;
    //     }

    //     char *buf = pt->toNorthBuffer->getBuffer();
    //     int nRet = recv(pt->south.soc, buf, bufSize, 0);
    //     if (nRet <= 0)
    //     {
    //         if (errno == EAGAIN)
    //         {
    //             // 此次数据接收已完毕
    //             break;
    //         }
    //         else
    //         {
    //             if (nRet == 0)
    //             {
    //                 // soc closed by peer
    //                 spdlog::debug("[UdpTunnel::southSocRecv] soc[{}] closed by south peer", pt->south.soc);
    //             }
    //             else
    //             {
    //                 spdlog::error("[UdpTunnel::southSocRecv] south soc[{}] recv fail: {}:[]",
    //                               pt->south.soc, errno, strerror(errno));
    //             }

    //             spdlog::trace("[UdpTunnel::southSocRecv] current stat: bufSize: {}, pt->toNorthBuffer: {}",
    //                           bufSize, pt->toNorthBuffer->toStr());

    //             pt->south.valid = false;

    //             return false;
    //         }
    //     }
    //     else
    //     {
    //         pt->toNorthBuffer->incDataSize(nRet);
    //     }
    // }

    return true;
}

bool UdpTunnel::southSocSend(UdpTunnel_t *pt)
{
    // while (uint64_t bufSize = pt->toSouthBuffer->dataSize())
    // {
    //     // send data to south
    //     char *buf = pt->toSouthBuffer->getData();
    //     int nRet = send(pt->south.soc, buf, bufSize, 0);
    //     if (nRet < 0)
    //     {
    //         if (errno == EAGAIN)
    //         {
    //             // 此次发送窗口已关闭
    //             break;
    //         }

    //         spdlog::debug("[UdpTunnel::southSocSend] soc[{}] send fail: {} - [{}]",
    //                       pt->south.soc, errno, strerror(errno));
    //         pt->south.valid = false;

    //         return false;
    //     }

    //     pt->toSouthBuffer->incFreeSize(nRet);
    // }

    return true;
}

bool UdpTunnel::isSameSockaddr(sockaddr *l, sockaddr *r)
{
    if (l->sa_family != r->sa_family)
    {
        return false;
    }

    if (l->sa_family == AF_INET)
    {
        sockaddr_in *lin = reinterpret_cast<sockaddr_in *>(l);
        sockaddr_in *rin = reinterpret_cast<sockaddr_in *>(r);
        if (ntohl(lin->sin_addr.s_addr) != ntohl(rin->sin_addr.s_addr) ||
            ntohs(lin->sin_port) != ntohs(rin->sin_port))
        {
            return false;
        }
    }
    else if (l->sa_family == AF_INET6)
    {
        sockaddr_in6 *lin6 = reinterpret_cast<sockaddr_in6 *>(l);
        sockaddr_in6 *rin6 = reinterpret_cast<sockaddr_in6 *>(r);
        if (memcmp(lin6->sin6_addr.s6_addr,
                   rin6->sin6_addr.s6_addr,
                   sizeof(lin6->sin6_addr.s6_addr)))
        {
            return false;
        }
        if (ntohs(lin6->sin6_port) != ntohs(rin6->sin6_port) ||
            lin6->sin6_flowinfo != rin6->sin6_flowinfo ||
            lin6->sin6_scope_id != rin6->sin6_scope_id)
        {
            return false;
        }
    }
    else
    {
        assert(!"unsupported type");
    }

    return true;
}

void UdpTunnel::appendToSendList(buffer::DynamicBuffer::BufBlk_t &listHead,
                                 buffer::DynamicBuffer::BufBlk_t *pBufBlk)
{
    if (listHead.prev)
    {
        // 链表中有数据
        pBufBlk->prev = listHead.prev;
        pBufBlk->next = nullptr;

        listHead.prev->next = pBufBlk;

        listHead.prev = pBufBlk;
    }
    else
    {
        // 当前链表为空
        pBufBlk->prev = pBufBlk->next = nullptr;
        listHead.prev = listHead.next = pBufBlk;
    }
}

} // namespace link
} // namespace mapper
