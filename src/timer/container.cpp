#include "container.h"
#include <spdlog/spdlog.h>

using namespace std;
using namespace spdlog;

namespace mapper
{
namespace timer
{

/**
 * timer state machine:
 * 
 *         *-----> INVALID
 *         |          |
 *         |          |
 *         |          V
 *         |       CONNECT -----> ESTABLISHED
 *         |          |               |
 *         |          |               |
 *         |          *-------V-------*
 *         |                  |
 *         |                  |
 *         |                  V
 *         *--------------- BROKEN
 */
bool Container::StateMachine[TYPE_COUNT][TYPE_COUNT] = {
    // INVALID, CONNECT, ESTABLISHED, BROKEN
    {1, 1, 0, 0}, // INVALID
    {0, 1, 1, 1}, // CONNECT
    {0, 0, 1, 1}, // ESTABLISHED
    {1, 0, 0, 1}, // BROKEN
};

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
    assert(c &&
           c->inTimer == false &&
           c->prev == nullptr &&
           c->next == nullptr &&
           checkStatChange(c, type));

    c->inTimer = true;
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

    assert(c->inTimer);

    Type_t type = c->type;

    if (c->prev)
    {
        c->prev->next = c->next;
    }
    else
    {
        assert(mHead[type] == c);

        mHead[type] = c->next;
    }

    if (c->next)
    {
        c->next->prev = c->prev;
    }
    else
    {
        assert(mTail[type] == c);

        mTail[type] = c->prev;
    }

    c->inTimer = false;
    c->prev = nullptr;
    c->next = nullptr;
}

Container::Client_t *Container::removeTimeout(Type_t type, time_t curTime)
{
    if (mTimeoutInterval[type] <= 0)
    {
        return nullptr;
    }

    Client_t *p = mHead[type];
    time_t timeoutTime = curTime - mTimeoutInterval[type];

    if (!p || p->time > timeoutTime)
    {
        return nullptr;
    }

    assert(p->prev == nullptr);
    p->inTimer = false;
    p->type = Type_t::TYPE_INVALID;

    Client_t *list = p;
    Client_t *last = p;
    p = p->next;
    while (p && p->time <= timeoutTime)
    {
        p->inTimer = false;
        p->type = Type_t::TYPE_INVALID;

        last = p;
        p = p->next;
    }

    last->next = nullptr;

    if (p)
    {
        p->prev = nullptr;
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
