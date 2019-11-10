#include "timeoutContainer.h"
#include <spdlog/spdlog.h>

using namespace std;
using namespace spdlog;

namespace mapper
{

TimeoutContainer::TimeoutContainer()
{
}

TimeoutContainer::~TimeoutContainer()
{
    if (!mList.empty())
    {
        warn("[TimeoutContainer::~TimeoutContainer] list NOT empty!");
    }
}

std::list<TimeoutContainer::Client *> TimeoutContainer::removeTimeout(time_t timePoint)
{
    list<Client *> list;

    for (auto *pItem : mList)
    {
        if (pItem->time <= timePoint)
        {
            list.push_back(pItem);
            continue;
        }
        break;
    }

    return move(list);
}

} // namespace mapper
