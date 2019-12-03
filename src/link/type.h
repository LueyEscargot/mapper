#ifndef __MAPPER_LINK_TYPE_H__
#define __MAPPER_LINK_TYPE_H__

#include <netdb.h>
#include <string.h>
#include <unistd.h>

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
    INITIALIZED = 1,
    CONNECT = 1 << 1,
    ESTABLISHED = 1 << 2,
    BROKEN = 1 << 3,
    CLOSED = 1 << 4
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

    inline void close()
    {
        ::close(soc);
        soc = 0;
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
        // base
        EndpointBase_t::init(Type_t::SERVICE, _soc);
        // protocol
        protocol = _protocol;
        // interface
        snprintf(interface, MAX_INTERFACE_NAME_LENGTH, "%s", _interface);
        // service
        snprintf(service, MAX_PORT_STR_LENGTH, "%s", _service);
        // targetHost
        snprintf(targetHost, MAX_HOST_NAME_LENGTH, "%s", _targetHost);
        // targetService
        snprintf(targetService, MAX_PORT_STR_LENGTH, "%s", _targetService);
        // Addresses list of targetHost
        targetHostAddrs = nullptr;
    }
} EndpointService_t;

typedef struct ENDPOINT_REMOTE : public EndpointBase_t
{
    void *tunnel;

    inline void init(Type_t type, int _soc, void *_tunnel)
    {
        // base
        EndpointBase_t::init(type, soc);
        // tunnel
        tunnel = _tunnel;
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

    inline void init(addrinfo *addrInfoList)
    {
        status = TunnelState_t::INITIALIZED;
        tag = nullptr;
        curAddr = addrInfoList;
    }
    inline void initAsConnect(int southSoc, int northSoc)
    {
        // south
        south.init(Type_t::SOUTH, southSoc, this);
        // north
        north.init(Type_t::SOUTH, northSoc, this);

        status = TunnelState_t::CONNECT;
    }
    inline void close()
    {
        south.close();
        north.close();

        status = TunnelState_t::CLOSED;
    }
} Tunnel_t;

typedef struct NAME_RESOLVE_BLOCK
{
    struct gaicb gaicb;
    struct addrinfo hints;
    char name[MAX_HOST_NAME_LENGTH];
    char serivce[MAX_PORT_STR_LENGTH];

    inline void init()
    {
        gaicb = (struct gaicb){};
        gaicb.ar_name = name;
        gaicb.ar_request = &hints;
        gaicb.ar_result = nullptr;
        gaicb.ar_service = serivce;
    }
    inline void init(const char *host, const int port, int socktype, int protocol, int flags)
    {
        snprintf(name, MAX_HOST_NAME_LENGTH, "%s", host);
        snprintf(serivce, MAX_PORT_STR_LENGTH, "%d", port);
        hints = (struct addrinfo){};
        hints.ai_socktype = socktype;
        hints.ai_protocol = protocol;
        hints.ai_flags = flags;
    }
} NameResolveBlk_t;

} // namespace link
} // namespace mapper

#endif // __MAPPER_LINK_TYPE_H__
