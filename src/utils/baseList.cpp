#include "baseList.h"
#include <assert.h>
#include <spdlog/spdlog.h>

namespace mapper
{
namespace utils
{

BaseList::BaseList()
    : mpHead(nullptr),
      mpTail(nullptr)
{
}

BaseList::~BaseList()
{
    if (mpHead)
    {
        spdlog::warn("[BaseList::~BaseList] list not empty");
    }
}

void BaseList::push_front(Entry_t *p)
{
    assert(!p->inList && !p->prev && !p->next);
    // spdlog::trace("[BaseList::push_front] [{}] push_front [{}]",
    //               (void *)this, (void *)p);

    if (!mpHead)
    {
        // 当前链表为空
        assert(!mpTail);
        mpHead = mpTail = p;
    }
    else
    {
        // 当前链表中已有节点
        assert(mpHead && !mpHead->prev);

        p->next = mpHead;
        mpHead->prev = p;
        mpHead = p;
    }

    p->inList = true;
}

void BaseList::push_back(Entry_t *p)
{
    assert(!p->inList &&
           !p->prev &&
           !p->next);
    // spdlog::trace("[BaseList::push_back] [{}] push_back [{}]",
    //               (void *)this, (void *)p);

    if (!mpHead)
    {
        // 当前链表为空
        assert(!mpTail);
        mpHead = mpTail = p;
    }
    else
    {
        // 当前链表中已有节点
        assert(mpTail &&
               !mpTail->next);
        p->prev = mpTail;
        mpTail->next = p;
        mpTail = p;
    }

    p->inList = true;
}

void BaseList::erase(Entry_t *p)
{
    assert(p->inList);
    // spdlog::trace("[BaseList::erase] [{}] erase [{}]",
    //               (void *)this, (void *)p);

    if (p->prev)
    {
        p->prev->next = p->next;
    }
    else
    {
        // 当前节点为头节点
        assert(mpHead == p);
        mpHead = p->next;
    }

    if (p->next)
    {
        p->next->prev = p->prev;
    }
    else
    {
        // 当前节点为尾节点
        assert(mpTail == p);
        mpTail = p->prev;
    }

    p->inList = false;
    p->prev = nullptr;
    p->next = nullptr;
}

bool BaseList::check()
{
    if (!mpHead)
    {
        assert(!mpTail);
        return true;
    }

    if (mpHead->prev)
    {
        spdlog::error("[BaseList::check] invalid head entry");
        return false;
    }
    if (mpTail->next)
    {
        spdlog::error("[BaseList::check] invalid tail entry");
        return false;
    }

    auto p = mpHead;
    while (p && p->next)
    {
        p = p->next;
    }

    if (p != mpTail->next)
    {
        spdlog::error("[BaseList::check] invalid list");
        return false;
    }

    return true;
}

} // namespace utils
} // namespace mapper
