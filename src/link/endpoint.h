#ifndef __MAPPER_LINK_ENDPOINT_H__
#define __MAPPER_LINK_ENDPOINT_H__

#include <netinet/in.h> // for sockaddr_in
#include <list>
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
    static const uint32_t BUFFER_SIZE = 1 << 10;
    static const uint32_t RELEASE_THRESHOLD = BUFFER_SIZE + 1 << 8;
    static const uint32_t BATCH_ALLOC_COUNT = 1 << 7;

protected:
    Endpoint(){};
    Endpoint(const Endpoint &){};
    Endpoint &operator=(const Endpoint &) { return *this; }

public:
    static Endpoint_t *getEndpoint(Protocol_t protocol, Direction_t direction, Type_t type);
    static void releaseEndpoint(Endpoint_t *pe);
    static bool appendToSendList(Endpoint_t *pe, DynamicBuffer::BufBlk_t *pBufBlk);
    static uint32_t sendListLength(const Endpoint_t *pe);

protected:
    static void batchAlloc(const uint32_t count);

    static uint32_t gFreeCount;
    static uint32_t gInUseCount;

    static std::list<Endpoint_t *> gFreeList;
};

} // namespace link
} // namespace mapper

#endif // __MAPPER_LINK_ENDPOINT_H__
