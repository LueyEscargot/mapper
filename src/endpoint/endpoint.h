#ifndef __MAPPER_ENDPOINT_ENDPOINT_H__
#define __MAPPER_ENDPOINT_ENDPOINT_H__

#include <netinet/in.h> // for sockaddr_in
#include <regex>
#include <string>
#include "type.h"

namespace mapper
{
namespace endpoint
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
                                            const char *servicePort,
                                            const char *targetHost,
                                            const char *targetPort);
    static bool createTunnel(const EndpointService_t *pes, EndpointTunnel_t *pet);

    static void releaseService(EndpointService_t *pService);

    static std::string toStr(EndpointBase_t *pEndpoint);
    static std::string toStr(EndpointService_t *pEndpoint);
    static std::string toStr(EndpointRemote_t *pEndpoint);
    static std::string toStr(EndpointTunnel_t *pEndpoint);

protected:
    static bool getIntfAddr(const char *intf, sockaddr_in &sa);
    static bool getAddrInfo(const EndpointService_t *pes, EndpointTunnel_t *pet);
    static bool connectToTarget(const EndpointService_t *pes, EndpointTunnel_t *pet);
};

} // namespace endpoint
} // namespace mapper

#endif // __MAPPER_ENDPOINT_ENDPOINT_H__
