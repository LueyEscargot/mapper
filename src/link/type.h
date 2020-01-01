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

typedef enum TYPE
{
    SERVICE = 1,
    NORTH = 1 << 1,
    SOUTH = 1 << 2
} Type_t;

typedef enum PROTOCOL
{
    UDP = 1,
    TCP = 1 << 1
} Protocol_t;

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

static const bool ENDPOINT_DIRECTION_NORTH = false;
static const bool ENDPOINT_DIRECTION_SOUTH = true;
static const bool ENDPOINT_TYPE_SERVICE = false;
static const bool ENDPOINT_TYPE_NORMAL = true;
static const bool ENDPOINT_INVALID = false;
static const bool ENDPOINT_VALID = true;
static const bool ENDPOINT_PROTOCOL_TCP = false;
static const bool ENDPOINT_PROTOCOL_UDP = true;

typedef struct ENDPOINT : public ENDPOINT_BASE
{
    bool direction : 1;
    bool type : 1;
    bool valid : 1;
    bool protocol : 1;
    int reverse : 4;

    int soc;
    sockaddr_in sockAddr;
    socklen_t sockAddrLen;
    addrinfo *targetHostAddrs;

    ENDPOINT *prev;
    ENDPOINT *next;
    ENDPOINT *peer;
    void *service;
    void *sendListHead;
    void *sendListTail;
    void *tag;

    inline void init()
    {
        //---------------------------------------------
        // TODO: remove this after refactory
        ENDPOINT_BASE::init(Protocol_t::UDP, Type_t::SERVICE, 0);
        //---------------------------------------------

        direction = ENDPOINT_DIRECTION_NORTH;
        type = ENDPOINT_TYPE_SERVICE;
        valid = ENDPOINT_INVALID;
        protocol = ENDPOINT_PROTOCOL_TCP;
        soc = 0;
        sockAddrLen = sizeof(sockaddr_in);
        targetHostAddrs = nullptr;
        prev = nullptr;
        next = nullptr;
        peer = nullptr;
        service = nullptr;
        sendListHead = nullptr;
        sendListTail = nullptr;
        tag = nullptr;
    }

    inline void setDirection(const bool toNorth) { direction = toNorth; }
    inline void setType(const bool isService) { type = isService; }
    inline void setValid(const bool isValid) { valid = isValid; }
    inline void setProtocol(const bool isTcp) { protocol = isTcp; }

    inline bool isToNorth() const { return direction == ENDPOINT_DIRECTION_NORTH; }
    inline bool isService() const { return type == ENDPOINT_TYPE_SERVICE; }
    inline bool isValid() const { return valid == ENDPOINT_VALID; }
    inline bool isTcp() const { return protocol == ENDPOINT_PROTOCOL_TCP; }
} Endpoint_t;

typedef struct UDP_TUNNEL
{
    time_t lastActiveTime;

    Endpoint_t *north;
    Endpoint_t *south;
    void *tag;
    void *service;

    TunnelState_t status;
    // addrinfo *curAddr;

    bool stopToNorthBufRecv;
    bool stopToSouthBufRecv;

    inline void init()
    {
        lastActiveTime = 0;

        north = nullptr;
        south = nullptr;
        tag = nullptr;
        service = nullptr;

        status = TunnelState_t::INITIALIZED;

        stopToNorthBufRecv = false;
        stopToSouthBufRecv = false;
    }
} UdpTunnel_t;

} // namespace link
} // namespace mapper

#endif // __MAPPER_LINK_TYPE_H__
