#ifndef __MAPPER_LINK_ENDPOINT_H__
#define __MAPPER_LINK_ENDPOINT_H__

#include <netinet/in.h> // for sockaddr_in
#include <string>
#include "type.h"
#include "../buffer/dynamicBuffer.h"

using namespace mapper::buffer;

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
    static Endpoint_t *getEndpoint(Protocol_t protocol, Direction_t direction, Type_t type);
    static void releaseEndpoint(Endpoint_t *pe);
    static void appendToSendList(Endpoint_t *pe, DynamicBuffer::BufBlk_t *pBufBlk);
    static uint32_t sendListLength(const Endpoint_t *pe);
};

} // namespace link
} // namespace mapper

#endif // __MAPPER_LINK_ENDPOINT_H__
