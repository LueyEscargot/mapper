#ifndef __MAPPER_LINK_ENDPOINT_H__
#define __MAPPER_LINK_ENDPOINT_H__

#include <netinet/in.h> // for sockaddr_in
#include <string>
#include "type.h"
#include "../buffer/dynamicBuffer.h"

namespace mapper
{
namespace link
{

class Endpoint
{
protected:
    Endpoint(){};
    Endpoint(const Endpoint &){};
    Endpoint &operator=(const Endpoint &) { return *this; }

public:
    static EndpointService_t *createService(const char *protocol,
                                            const char *intf,
                                            const char *service,
                                            const char *targetHost,
                                            const char *targetService);
    static void releaseService(EndpointService_t *pService);

    static std::string toStr(const EndpointBase_t *pEndpoint);
    static std::string toStr(const EndpointService_t *pEndpoint);
    static std::string toStr(const EndpointRemote_t *pEndpoint);
    static std::string toStr(const Tunnel_t *pEndpoint);

    static Endpoint_t *getEndpoint(Protocol_t protocol,
                                   Direction_t direction,
                                   Type_t type);
    static void releaseEndpoint(Endpoint_t *pe);
    static void appendToSendList(Endpoint_t *pe,
                                 buffer::DynamicBuffer::BufBlk_t *pBufBlk);

protected:
    static bool getIntfAddr(const char *intf, sockaddr_in &sa);
    static bool getAddrInfo(EndpointService_t *pes);
    static int createTcpServiceSoc(sockaddr_in &sa);
};

} // namespace link
} // namespace mapper

#endif // __MAPPER_LINK_ENDPOINT_H__
