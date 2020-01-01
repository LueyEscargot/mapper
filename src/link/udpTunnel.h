/**
 * @file udpTunnel.h
 * @author Liu Yu (source@liuyu.com)
 * @brief Network events manager.
 * @version 1.0
 * @date 2019-12-26
 * 
 * @copyright Copyright (c) 2019
 * 
 */
#ifndef __MAPPER_LINK_UDPTUNNEL_H__
#define __MAPPER_LINK_UDPTUNNEL_H__

#include <sys/socket.h>
#include "type.h"
#include "../buffer/dynamicBuffer.h"

namespace mapper
{
namespace link
{

class UdpTunnel
{
protected:
    static const uint32_t MAX_RESERVE_SIZE = 1 << 16;

    UdpTunnel() = default;
    UdpTunnel(const UdpTunnel &) = default;
    UdpTunnel &operator=(const UdpTunnel &) { return *this; }

public:
    static bool init(UdpTunnel_t *pt,
                     EndpointService_t *pes,
                     sockaddr_storage *south,
                     socklen_t south_len,
                     buffer::DynamicBuffer::BufBlk_t *toNorthBuf);
    static void setStatus(UdpTunnel_t *pt, TunnelState_t stat);
    static std::string toStr(UdpTunnel_t *pt);

    static bool connect(UdpTunnel_t *pt);
    static bool northSocRecv(UdpTunnel_t *pt);
    static bool northSocSend(UdpTunnel_t *pt);
    static bool southSocRecv(UdpTunnel_t *pt);
    static bool southSocSend(UdpTunnel_t *pt);

protected:
    static bool isSameSockaddr(sockaddr *l, sockaddr *r);
    static void appendToSendList(buffer::DynamicBuffer::BufBlk_t &listHead,
                                 buffer::DynamicBuffer::BufBlk_t *pBufBlk);

    static const bool StateMaine[TUNNEL_STATE_COUNT][TUNNEL_STATE_COUNT];
};

} // namespace link
} // namespace mapper

#endif // __MAPPER_LINK_UDPTUNNEL_H__
