#ifndef __MAPPER_ENDPOINT_TYPE_H__
#define __MAPPER_ENDPOINT_TYPE_H__

#include <netdb.h>
#include <string.h>

namespace mapper
{
namespace endpoint
{

static const uint32_t MAX_INTERFACE_NAME_LENGTH = 64;
static const uint32_t MAX_HOST_NAME_LENGTH = 256;
static const uint32_t MAX_PORT_LENGTH = 6;

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
    INITIALIZED = 1,
    CONNECT = 1 << 1,
    ESTABLISHED = 1 << 2,
    BROKEN = 1 << 3
} TunnelState_t;

typedef struct ENDPOINT_BASE
{
    inline void init(Type_t _type, int _soc)
    {
        type = _type;
        soc = _soc;
        valid = true;
    }

    Type_t type;
    int soc;
    bool valid;
} EndpointBase_t;

typedef struct ENDPOINT_SERVICE : public EndpointBase_t
{
    Protocol_t protocol;
    char interface[MAX_INTERFACE_NAME_LENGTH];
    char servicePort[MAX_PORT_LENGTH];
    char targetHost[MAX_HOST_NAME_LENGTH];
    char targetPort[MAX_PORT_LENGTH];

    inline void init(int _soc,
                     Protocol_t _protocol,
                     const char *_interface,
                     const char *_servicePort,
                     const char *_targetHost,
                     const char *_targetPort)
    {
        // base
        EndpointBase_t::init(Type_t::SERVICE, soc);
        // protocol
        protocol = _protocol;
        // interface
        snprintf(interface, MAX_INTERFACE_NAME_LENGTH, "%s", _interface);
        // servicePort
        snprintf(servicePort, MAX_PORT_LENGTH, "%s", _servicePort);
        // targetHost
        snprintf(targetHost, MAX_HOST_NAME_LENGTH, "%s", _targetHost);
        // targetPort
        snprintf(targetPort, MAX_PORT_LENGTH, "%s", _targetPort);
    }
} EndpointService_t;

typedef struct ENDPOINT_REMOTE : public EndpointBase_t
{
    void *tunnel;

    void init(Type_t type, int _soc, void *_tunnel)
    {
        // base
        EndpointBase_t::init(type, soc);
        // tunnel
        tunnel = _tunnel;
    }
} EndpointRemote_t;

typedef struct ENDPOINT_TUNNEL
{
    EndpointRemote_t south;
    EndpointRemote_t north;
    TunnelState_t status;
    void *tag;

    addrinfo *addrHead;
    addrinfo *curAddr;

    void init(int southSoc, int northSoc)
    {
        // south
        south.init(Type_t::SOUTH, southSoc, this);
        // north
        north.init(Type_t::SOUTH, northSoc, this);

        status = TunnelState_t::INITIALIZED;
        tag = nullptr;

        addrHead = nullptr;
        curAddr = nullptr;
    }
} EndpointTunnel_t;

} // namespace endpoint
} // namespace mapper

#endif // _H__
