#include "timerList.h"
#include <assert.h>
#include <spdlog/spdlog.h>

namespace mapper
{
namespace utils
{

void TimerList::refresh(time_t curTime, Entry_t *p)
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

} // namespace utils
} // namespace mapper
