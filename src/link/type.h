/**
 * @file type.h
 * @author Liu Yu (source@liuyu.com)
 * @brief Type defines.
 * @version 2
 * @date 2019-12-05
 * 
 * @copyright Copyright (c) 2019
 * 
 */
#ifndef __MAPPER_LINK_TYPE_H__
#define __MAPPER_LINK_TYPE_H__

#include <stdint.h>
#include <netinet/in.h>
#include "../utils/timerList.h"

namespace mapper
{
namespace link
{

enum Type_t
{
    TYPE_INVALID = 0,
    TYPE_SERVICE,
    TYPE_NORMAL
};

enum Protocol_t
{
    PROTOCOL_UNKNOWN = 0,
    PROTOCOL_UDP,
    PROTOCOL_TCP,
};

enum Direction_t
{
    TO_UNKNOWN = 0,
    TO_NORTH,
    TO_SOUTH
};

enum TunnelState_t
{
    TUNSTAT_CLOSED = 0,
    TUNSTAT_INITIALIZED,
    TUNSTAT_CONNECT,
    TUNSTAT_ESTABLISHED,
    TUNSTAT_BROKEN,
    TUNNEL_STATE_COUNT
};

struct Connection_t
{
    Protocol_t protocol;
    sockaddr_in localAddr;
    socklen_t localAddrLen;
    sockaddr_in remoteAddr;
    socklen_t remoteAddrLen;

    inline Connection_t() { init(PROTOCOL_UNKNOWN); }
    inline Connection_t(const Connection_t &src)
    {
        protocol = src.protocol;
        localAddr = src.localAddr;
        remoteAddr = src.remoteAddr;
    }
    inline void init(Protocol_t protocol)
    {
        protocol = protocol;
        localAddr = {0};
        remoteAddr = {0};
    }
    inline Connection_t &operator=(const Connection_t &src)
    {
        protocol = src.protocol;
        localAddr = src.localAddr;
        remoteAddr = src.remoteAddr;
        return *this;
    }
};

struct Endpoint_t
{
    Direction_t direction;
    Type_t type;
    bool valid;
    bool stopRecv;

    int soc;
    Connection_t conn;

    utils::TimerList::Entity_t bufWaitEntry;

    Endpoint_t *prev;
    Endpoint_t *next;
    Endpoint_t *peer;
    void *service;
    void *container;
    void *sendListHead;
    void *sendListTail;

    int64_t totalBufSize;
    bool bufferFull;

    Endpoint_t(){};
    inline void init(Protocol_t protocol, Direction_t _direction, Type_t _type)
    {
        direction = _direction;
        type = _type;
        valid = true;
        stopRecv = false;

        soc = 0;
        conn.init(protocol);

        bufWaitEntry.init(this);

        prev = nullptr;
        next = nullptr;
        peer = nullptr;
        service = nullptr;
        container = nullptr;
        sendListHead = nullptr;
        sendListTail = nullptr;

        totalBufSize = 0;
        bufferFull = false;
    }
};

struct Tunnel_t
{
    utils::TimerList::Entity_t  timerEntity;

    Endpoint_t *north;
    Endpoint_t *south;
    void *service;

    TunnelState_t stat;

    inline void init()
    {
        timerEntity.init(this);

        north = nullptr;
        south = nullptr;
        service = nullptr;

        stat = TUNSTAT_CLOSED;
    }
};

} // namespace link
} // namespace mapper

#endif // __MAPPER_LINK_TYPE_H__
