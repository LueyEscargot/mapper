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

#include <netdb.h>
#include <sys/socket.h>
#include <string.h>
#include <unistd.h>
#include "../buffer/buffer.h"
#include "../buffer/dynamicBuffer.h"
#include "../timer/container.h"

namespace mapper
{
namespace link
{

class UdpForwardService;

static const uint32_t MAX_INTERFACE_NAME_LENGTH = 64;
static const uint32_t MAX_HOST_NAME_LENGTH = 256;
static const uint32_t MAX_PORT_STR_LENGTH = 6;

enum Type_t
{
    SERVICE = 1,
    NORTH = 1 << 1, // TODO: remove
    SOUTH = 1 << 2, // TODO: remove
    NORMAL
};

enum Protocol_t
{
    UDP,
    TCP,
    UNKNOWN_PROTOCOL
};

enum Direction_t
{
    DIR_NORTH,
    DIR_SOUTH
};

typedef enum TUNNEL_STATE
{
    CLOSED = 0,
    ALLOCATED,
    INITIALIZED,
    CONNECT,
    ESTABLISHED,
    BROKEN,
    TUNNEL_STATE_COUNT
} TunnelState_t;

typedef struct ENDPOINT_BASE
{
    Protocol_t protocol;
    Type_t type;
    int soc;
    bool valid;

    inline void init(Protocol_t _protocol, Type_t _type, int _soc)
    {
        protocol = _protocol;
        type = _type;
        setSoc(_soc);
    }
    inline void setSoc(int _soc)
    {
        soc = _soc;
        valid = true;
    }

    inline void close()
    {
        if (soc > 0)
        {
            ::close(soc);
            soc = 0;
        }
        valid = false;
    }
} EndpointBase_t;

typedef struct ENDPOINT_SERVICE : public EndpointBase_t
{
    char interface[MAX_INTERFACE_NAME_LENGTH];
    char service[MAX_PORT_STR_LENGTH];
    char targetHost[MAX_HOST_NAME_LENGTH];
    char targetService[MAX_PORT_STR_LENGTH];
    addrinfo *targetHostAddrs;
    link::UdpForwardService *udpService;

    inline void init(int _soc,
                     Protocol_t protocol,
                     const char *_interface,
                     const char *_service,
                     const char *_targetHost,
                     const char *_targetService)
    {
        EndpointBase_t::init(protocol, Type_t::SERVICE, _soc);              // base
        snprintf(interface, MAX_INTERFACE_NAME_LENGTH, "%s", _interface);   // interface
        snprintf(service, MAX_PORT_STR_LENGTH, "%s", _service);             // service
        snprintf(targetHost, MAX_HOST_NAME_LENGTH, "%s", _targetHost);      // targetHost
        snprintf(targetService, MAX_PORT_STR_LENGTH, "%s", _targetService); // targetService
        targetHostAddrs = nullptr;                                          // addresses list of targetHost
        udpService = nullptr;                                               // UDP tunnel manager
    }
    inline void close() {}
} EndpointService_t;

typedef struct ENDPOINT_REMOTE : public EndpointBase_t
{
    void *tunnel;

    inline void init(Protocol_t protocol, Type_t type, void *_tunnel)
    {
        // base
        EndpointBase_t::init(protocol, type, 0);
        // tunnel
        tunnel = _tunnel;
    }
    inline void setSoc(int _soc)
    {
        // base
        EndpointBase_t::setSoc(_soc);
    }
    inline void close()
    {
        EndpointBase_t::close();
    }
} EndpointRemote_t;

typedef struct TUNNEL
{
    timer::Container::Client_t timerClient;
    EndpointRemote_t south;
    EndpointRemote_t north;
    TunnelState_t status;
    void *tag;
    addrinfo *curAddr;

    buffer::Buffer *toNorthBuffer;
    buffer::Buffer *toSouthBuffer;

    inline void create(buffer::Buffer *_toNorthBuffer, buffer::Buffer *_toSouthBuffer)
    {
        timerClient.time = 0;
        timerClient.prev = nullptr;
        timerClient.next = nullptr;
        timerClient.tag = this;

        south.init(Protocol_t::TCP, Type_t::SOUTH, this);
        north.init(Protocol_t::TCP, Type_t::NORTH, this);
        status = TunnelState_t::CLOSED; // set initial status
        tag = nullptr;
        curAddr = nullptr;
        toNorthBuffer = _toNorthBuffer;
        toSouthBuffer = _toSouthBuffer;
    }
    inline void init(int southSoc, int northSoc)
    {
        timerClient.init();
        south.setSoc(southSoc);
        north.setSoc(northSoc);
        toNorthBuffer->init();
        toSouthBuffer->init();
    }
    inline void setAddrInfo(addrinfo *ai)
    {
        curAddr = ai;
    }
    inline void close()
    {
        south.close();
        north.close();
    }
} Tunnel_t;

struct IpTuple_t
{
    Protocol_t p;  // protocol
    sockaddr_in l; // local
    sockaddr_in r; // remote

    void init(Protocol_t protocol)
    {
        p = protocol;
        l = {0};
        r = {0};
    }
};

struct Endpoint_t : public ENDPOINT_BASE
{
    Direction_t direction;
    Type_t type;
    bool valid;

    int soc;
    IpTuple_t ipTuple;

    Endpoint_t *prev;
    Endpoint_t *next;
    Endpoint_t *peer;
    void *service;
    void *container;
    void *sendListHead;
    void *sendListTail;
    void *tag;

    inline void init(Protocol_t protocol,
                     Direction_t _direction,
                     Type_t _type)
    {
        //---------------------------------------------
        // TODO: remove this after refactory
        ENDPOINT_BASE::init(protocol, _type, 0);
        //---------------------------------------------

        direction = _direction;
        type = _type;
        valid = true;

        soc = 0;
        ipTuple.init(protocol);

        prev = nullptr;
        next = nullptr;
        peer = nullptr;
        service = nullptr;
        container = nullptr;
        sendListHead = nullptr;
        sendListTail = nullptr;
        tag = nullptr;
    }
};

struct TunnelTimer_t
{
    time_t lastActiveTime;
    TunnelTimer_t *prev;
    TunnelTimer_t *next;

    void *tunnel;

    void init(void *_tunnel)
    {
        lastActiveTime = 0;
        prev = nullptr;
        next = nullptr;

        tunnel = _tunnel;
    }
};

struct UdpTunnel_t
{
    TunnelTimer_t timer;

    Endpoint_t *north;
    Endpoint_t *south;
    void *service;

    inline void init()
    {
        timer.init(this);

        north = nullptr;
        south = nullptr;
        service = nullptr;
    }
};

} // namespace link
} // namespace mapper

#endif // __MAPPER_LINK_TYPE_H__
