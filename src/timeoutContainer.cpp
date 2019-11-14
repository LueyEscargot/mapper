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

std::list<TimeoutContainer::Client *> TimeoutContainer::removeTimeout(time_t timeoutTime)
{
    list<Client *> timeoutList;
    auto it = mList.begin();
    while (it != mList.end())
    {
        if ((*it)->time > timeoutTime)
        {
            break;
        }

        timeoutList.push_back(*it);
        (*it)->mpContainer = nullptr;

        auto delIt = it;
        ++it;
        mList.erase(delIt);
    }

    return move(timeoutList);
}

} // namespace mapper
