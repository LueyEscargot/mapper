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
    static const uint32_t BUFFER_SIZE = 1 << 10;
    static const uint32_t RELEASE_THRESHOLD = BUFFER_SIZE + 1 << 8;
    static const uint32_t BATCH_ALLOC_COUNT = 1 << 7;

protected:
    Tunnel() = default;
    Tunnel(const Tunnel &) = default;
    Tunnel &operator=(const Tunnel &) { return *this; }

public:
    static Tunnel_t *getTunnel();
    static void releaseTunnel(Tunnel_t *pt);

protected:
    static void batchAlloc(const uint32_t count);

    static uint32_t gFreeCount;
    static uint32_t gInUseCount;

    static std::list<Tunnel_t *> gFreeList;
};

} // namespace link
} // namespace mapper

#endif // __MAPPER_LINK_TUNNEL_H__
