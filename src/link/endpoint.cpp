#include "endpoint.h"
#include <assert.h>
#include <fcntl.h>
#include <ifaddrs.h>
#include <netdb.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <sstream>
#include <spdlog/spdlog.h>

using namespace std;
using namespace mapper::buffer;

namespace mapper
{
namespace link
{

Endpoint_t *Endpoint::getEndpoint(Protocol_t protocol, Direction_t direction, Type_t type)
{
    // TODO: use endpoint buffer
    Endpoint_t *pe = new mapper::link::Endpoint_t;
    if (pe)
    {
        pe->init(protocol, direction, type);
    }

    return pe;
}

void Endpoint::releaseEndpoint(Endpoint_t *pe)
{
    // TODO: use endpoint buffer
    delete pe;
}

void Endpoint::appendToSendList(Endpoint_t *pe, DynamicBuffer::BufBlk_t *pBufBlk)
{
    if (pe->sendListTail)
    {
        // 链表中有数据
        auto pTail = static_cast<DynamicBuffer::BufBlk_t *>(pe->sendListHead);

        pBufBlk->prev = pTail;
        pBufBlk->next = nullptr;

        pTail->next = pBufBlk;

        pe->sendListTail = pBufBlk;
    }
    else
    {
        // 当前链表为空
        pBufBlk->prev = pBufBlk->next = nullptr;
        pe->sendListHead = pe->sendListTail = pBufBlk;
    }
}

} // namespace link
} // namespace mapper
