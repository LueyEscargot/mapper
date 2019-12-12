#include "container.h"
#include <spdlog/spdlog.h>

using namespace std;
using namespace spdlog;

namespace mapper
{
namespace timer
{

Container::Container()
    : tail(&head)
{
    head.next = nullptr;
    for (int i = 0; i < Type_t::TYPE_COUNT; ++i)
    {
        mHead[i] = mTail[i] = nullptr;
    }
};

Container::~Container()
{
    if (!empty())
    {
        warn("[Container::~Container] timer container NOT empty!");
    }
}

void Container::insert(Type_t type, time_t t, Client_t *c)
{
    c->time = t;
    c->type = type;
    c->next = nullptr;

    // insert at end of list and shift pointer 'tail' to new inserted item
    if (mHead[type])
    {
        assert(mTail[type] == nullptr);

        c->prev = nullptr;
        mHead[type] = mTail[type] = c;
    }
    else
    {
        assert(mTail[type]);

        c->prev = mTail[type];
        mTail[type]->next = c;
    }
}

void Container::remove(Client_t *c)
{
    if (c->prev)
    {
        c->prev->next = c->next;
        c->prev = nullptr;
    }
    else
    {
        mHead[c->type] = c->next;
    }

    if (c->next)
    {
        c->next->prev = c->prev;
        c->next = nullptr;
    }
    else
    {
        mTail[c->type] = c->prev;
    }
}

Container::Client_t *Container::removeTimeout(Type_t type, time_t timeoutTime)
{
    Client_t *p = mHead[type];

    if (!p || p->time > timeoutTime)
    {
        return nullptr;
    }

    assert(p->prev = nullptr);
    Client_t *list = p;

    Client_t *last = p;
    p = p->next;
    while (p && p->time <= timeoutTime)
    {
        last = p;
        p = p->next;
    }

    last->next = nullptr;

    if (p)
    {
        mHead[type] = p;
    }
    else
    {
        mHead[type] = mTail[type] = nullptr;
    }

    return list;
}

Container::Client_t *Container::removeTimeout(time_t timeoutTime)
{
    Client_t *p = head.next;

    if (!p || p->time > timeoutTime)
    {
        return nullptr;
    }

    Client_t *list = p;
    p->prev = nullptr;
    Client_t *last = p;
    p = p->next;

    while (p && p->time <= timeoutTime)
    {
        last = p;
        p = p->next;
    }

    last->next = nullptr;

    head.next = p;
    if (p)
    {
        p->next->prev = nullptr;
    }

    return list;
}

} // namespace timer
} // namespace mapper
