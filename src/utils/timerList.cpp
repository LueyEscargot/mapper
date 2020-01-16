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
