/**
 * @file tunnel.h
 * @author Liu Yu (source@liuyu.com)
 * @brief Network events manager.
 * @version 1.0
 * @date 2019-12-05
 * 
 * @copyright Copyright (c) 2019
 * 
 */
#ifndef __MAPPER_LINK_TUNNEL_H__
#define __MAPPER_LINK_TUNNEL_H__

#include "type.h"

namespace mapper
{
namespace link
{

class Tunnel
{
protected:
    Tunnel() = default;
    Tunnel(const Tunnel &) = default;
    Tunnel &operator=(const Tunnel &) { return *this; }

public:
    static bool init(Tunnel_t *pt, EndpointService_t *pes, int southSoc);
    static void setStatus(Tunnel_t *pt, TunnelState_t stat);
    static std::string toStr(Tunnel_t *pt);

    static UdpTunnel_t *getTunnel();
    static void releaseTunnel(UdpTunnel_t *pe);

    static bool connect(Tunnel_t *pt);
    static bool northSocRecv(Tunnel_t *pt);
    static bool northSocSend(Tunnel_t *pt);
    static bool southSocRecv(Tunnel_t *pt);
    static bool southSocSend(Tunnel_t *pt);

    static const bool StateMaine[TUNNEL_STATE_COUNT][TUNNEL_STATE_COUNT];
};

} // namespace link
} // namespace mapper

#endif // __MAPPER_LINK_TUNNEL_H__
