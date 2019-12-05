#ifndef __MAPPER_LINK_TYPE_H__
#define __MAPPER_LINK_TYPE_H__

#include <netdb.h>
#include <string.h>
#include <unistd.h>
#include "../buffer/buffer.h"

namespace mapper
{
namespace link
{

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
    CREATED = 0,
    ALLOCATED = 1,
    INITIALIZED = 1 << 1,
    CONNECT = 1 << 2,
    ESTABLISHED = 1 << 3,
    BROKEN = 1 << 4,
    CLOSED = 1 << 5,
} TunnelState_t;

typedef struct ENDPOINT_BASE
{
    inline void init(Type_t _type, int _soc)
    {
        type = _type;
        setSoc(_soc);
    }
    inline void setSoc(int _soc)
    {
        soc = _soc;
        valid = true;
    }

    Type_t type;
    int soc;
    bool valid;

    inline void close()
    {
        if (soc > 0)
            ::close(soc);
        valid = false;
    }
} EndpointBase_t;

typedef struct ENDPOINT_SERVICE : public EndpointBase_t
{
    Protocol_t protocol;
    char interface[MAX_INTERFACE_NAME_LENGTH];
    char service[MAX_PORT_STR_LENGTH];
    char targetHost[MAX_HOST_NAME_LENGTH];
    char targetService[MAX_PORT_STR_LENGTH];
    addrinfo *targetHostAddrs;

    inline void init(int _soc,
                     Protocol_t _protocol,
                     const char *_interface,
                     const char *_service,
                     const char *_targetHost,
                     const char *_targetService)
    {
        EndpointBase_t::init(Type_t::SERVICE, _soc);                        // base
        protocol = _protocol;                                               // protocol
        snprintf(interface, MAX_INTERFACE_NAME_LENGTH, "%s", _interface);   // interface
        snprintf(service, MAX_PORT_STR_LENGTH, "%s", _service);             // service
        snprintf(targetHost, MAX_HOST_NAME_LENGTH, "%s", _targetHost);      // targetHost
        snprintf(targetService, MAX_PORT_STR_LENGTH, "%s", _targetService); // targetService
        targetHostAddrs = nullptr;                                          // Addresses list of targetHost
    }
} EndpointService_t;

typedef struct ENDPOINT_REMOTE : public EndpointBase_t
{
    void *tunnel;

    inline void init(Type_t type, void *_tunnel)
    {
        // base
        EndpointBase_t::init(type, 0);
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
    EndpointRemote_t south;
    EndpointRemote_t north;
    TunnelState_t status;
    void *tag;
    addrinfo *curAddr;

    buffer::Buffer *toNorthBUffer;
    buffer::Buffer *toSouthBUffer;

    inline void create(buffer::Buffer *_toNorthBUffer, buffer::Buffer *_toSouthBUffer)
    {
        south.init(Type_t::SOUTH, this);
        north.init(Type_t::NORTH, this);
        tag = nullptr;
        curAddr = nullptr;
        toNorthBUffer = _toNorthBUffer;
        toSouthBUffer = _toSouthBUffer;

        setStatus(TunnelState_t::CREATED);
    }
    inline void init(int southSoc, int northSoc, addrinfo *addrInfoList)
    {
        south.setSoc(southSoc);
        north.setSoc(northSoc);
        curAddr = addrInfoList;
        toNorthBUffer->init();
        toSouthBUffer->init();

        setStatus(TunnelState_t::INITIALIZED);
    }
    inline void setStatus(TunnelState_t _status)
    {
        status = _status;
    }
    inline void close()
    {
        south.close();
        north.close();

        setStatus(TunnelState_t::CLOSED);
    }
} Tunnel_t;

} // namespace link
} // namespace mapper

#endif // __MAPPER_LINK_TYPE_H__
