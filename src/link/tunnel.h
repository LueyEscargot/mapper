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
    static Tunnel_t *getTunnel();
    static void releaseTunnel(Tunnel_t *pt);
};

} // namespace link
} // namespace mapper

#endif // __MAPPER_LINK_TUNNEL_H__
