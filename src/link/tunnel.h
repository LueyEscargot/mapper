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

#include <functional>
#include "type.h"

namespace mapper
{
namespace link
{

class Tunnel
{
protected:
    using CB_SetEpollMode = std::function<bool(EndpointRemote_t*, bool, bool, bool)>;

    Tunnel() = default;
    Tunnel(const Tunnel &) = default;
    Tunnel &operator=(const Tunnel &) { return *this; }

public:
    static bool init(Tunnel_t *pt, EndpointService_t *pes, int southSoc);
    static void setStatus(Tunnel_t *pt, TunnelState_t stat);
    static std::string toStr(Tunnel_t *pt);

    static bool connect(Tunnel_t *pt);
    static bool onSoc(uint64_t curTime, EndpointRemote_t *per, uint32_t events, CB_SetEpollMode cbSetEpollMode);
    static bool northSocRecv(Tunnel_t *pt, CB_SetEpollMode cbSetEpollMode);
    static bool northSocSend(Tunnel_t *pt, CB_SetEpollMode cbSetEpollMode);
    static bool southSocRecv(Tunnel_t *pt, CB_SetEpollMode cbSetEpollMode);
    static bool southSocSend(Tunnel_t *pt, CB_SetEpollMode cbSetEpollMode);

    static const bool StateMaine[TUNNEL_STATE_COUNT][TUNNEL_STATE_COUNT];
};

} // namespace link
} // namespace mapper

#endif // __MAPPER_LINK_TUNNEL_H__
