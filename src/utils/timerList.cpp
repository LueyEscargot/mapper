#include "timerList.h"
#include <assert.h>
#include <spdlog/spdlog.h>

using namespace std;

namespace mapper
{
namespace utils
{

void TimerList::refresh(time_t curTime, Entity_t *p)
{
    if (p->time == curTime) // 时间相同
    {
        // 无需调整位置
        return;
    }
    else
    {
        p->time = curTime;
        if (mpTail == p) // 是否为最后一个节点
        {
            // 无需调整位置
            return;
        }
        else if (((TimerList::Entity_t *)(p->next))->time == p->time) // p 非最后一个元素
        {
            // 在有序链表中，后面的元素的时间与当前元素更新后的时间一致，所以无需调整位置
            assert(((TimerList::Entity_t *)mpTail)->time == p->time);
            return;
        }
        else
        {
            BaseList::erase(p);
            BaseList::push_back(p);
        }
    }
}

void TimerList::removeTimeout(time_t timeoutTime, list<Entity_t *> &timeoutList)
{
    while (mpHead)
    {
        auto entity = (TimerList::Entity_t *)mpHead;
        mpHead = mpHead->next;

        if (entity->time <= timeoutTime)
        {
            // 当前元素超时
            entity->inList = false;
            entity->prev = entity->next = nullptr;
            timeoutList.push_back(entity);
            // spdlog::trace("[TimerList::removeTimeout] get timeout entity[{}]", (void *)entity);
        }
        else
        {
            break;
        }
    }

    if (mpHead)
    {
        // 当前链表不为空
        if (mpHead->prev)
        {
            // 至少有一个元素超时
            mpHead->prev = nullptr;
        }
        else
        {
            // 没有超时元素
            assert(timeoutList.empty());
        }
    }
    else
    {
        // 当前链表为空
        mpTail = nullptr;
    }
}

void TimerList::getTimeoutList(time_t timeoutTime, list<Entity_t *> &timeoutList)
{
    auto p = (TimerList::Entity_t *)mpHead;

    while (p)
    {
        if (p->time > timeoutTime)
        {
            break;
        }

        // 当前元素超时
        timeoutList.push_back(p);
        p = (TimerList::Entity_t *)p->next;
    }
}

} // namespace utils
} // namespace mapper
