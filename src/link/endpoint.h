#ifndef __MAPPER_LINK_ENDPOINT_H__
#define __MAPPER_LINK_ENDPOINT_H__

#include <netinet/in.h> // for sockaddr_in
#include <regex>
#include <string>
#include "type.h"

namespace mapper
{
namespace link
{

class Endpoint
{
protected:
    static std::regex REG_FORWARD_SETTING_STRING;

    Endpoint(){};
    Endpoint(const Endpoint &){};
    Endpoint &operator=(const Endpoint &) { return *this; }

public:
    static EndpointService_t *createService(std::string forward);
    static EndpointService_t *createService(const char *protocol,
                                            const char *intf,
                                            const char *service,
                                            const char *targetHost,
                                            const char *targetService);
    static void releaseService(EndpointService_t *pService);
    static bool createTunnel(const EndpointService_t *pes, Tunnel_t *pt);


    static std::string toStr(const EndpointBase_t *pEndpoint);
    static std::string toStr(const EndpointService_t *pEndpoint);
    static std::string toStr(const EndpointRemote_t *pEndpoint);
    static std::string toStr(const Tunnel_t *pEndpoint);

protected:
    static bool getIntfAddr(const char *intf, sockaddr_in &sa);
    static bool getAddrInfo(const EndpointService_t *pes, Tunnel_t *pt);
    static bool connectToTarget(const EndpointService_t *pes, Tunnel_t *pt);
};

} // namespace link
} // namespace mapper

#endif // __MAPPER_LINK_ENDPOINT_H__
