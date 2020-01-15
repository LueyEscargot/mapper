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

} // namespace utils
} // namespace mapper
