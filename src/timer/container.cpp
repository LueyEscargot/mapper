#include "container.h"
#include <spdlog/spdlog.h>

using namespace std;
using namespace spdlog;

namespace mapper
{
namespace timer
{

Container::Container()
{
    for (int type = 0; type < Type_t::TYPE_COUNT; ++type)
    {
        mHead[type] = mTail[type] = nullptr;
    }
};

Container::~Container()
{
    for (int type = 0; type < Type_t::TYPE_COUNT; ++type)
    {
        if (!empty(static_cast<Type_t>(type)))
        {
            warn("[Container::~Container] timer container[{}] NOT empty!", type);
        }
    }
}

void Container::insert(Type_t type, time_t t, Client_t *c)
{
    assert(c);

    c->time = t;
    c->type = type;
    c->prev = mTail[type];
    c->next = nullptr;

    // insert at end of list and shift pointer 'tail' to new inserted item
    if (mHead[type])
    {
        assert(mTail[type]);

        mTail[type]->next = c;
    }
    else
    {
        assert(mTail[type] == nullptr);

        mHead[type] = c;
    }
    mTail[type] = c;
}

void Container::remove(Client_t *c)
{
    if (!c)
    {
        return;
    }

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

Container::Client_t *Container::removeTimeout(Type_t type, time_t curTime)
{
    Client_t *p = mHead[type];
    time_t timeoutTime = curTime - mTimeoutInterval[type];

    if (!p || p->time > timeoutTime)
    {
        return nullptr;
    }

    assert(p->prev == nullptr);
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

bool Container::isInTimer(Type_t type, Client_t *pClient)
{
    return pClient->type == type;
}

} // namespace timer
} // namespace mapper
