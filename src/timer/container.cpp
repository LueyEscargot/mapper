#include "container.h"
#include <spdlog/spdlog.h>

using namespace std;
using namespace spdlog;

namespace mapper
{
namespace timer
{

Container::~Container()
{
    if (!empty())
    {
        warn("[Container::~Container] timer container NOT empty!");
    }
}

Container::Client_t *Container::removeTimeout(time_t timeoutTime)
{
    Client_t *p = head.next;

    if (!p || p->time > timeoutTime) {
        return nullptr;
    }

    Client_t *list = p;
    p->prev = nullptr;
    p->container = nullptr;
    Client_t *last = p;
    p = p->next;

    while (p && p->time <= timeoutTime)
    {
        p->container = nullptr;
        last = p;
        p = p->next;
    }

    last->next = nullptr;

    head.next = p;
    if (p) {
        p->next->prev = nullptr;
    }

    return list;
}

} // namespace timer
} // namespace mapper
