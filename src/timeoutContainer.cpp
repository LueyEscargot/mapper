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
    if (!mContainer.empty())
    {
        warn("[TimeoutContainer::~TimeoutContainer] list NOT empty!");
    }
}

TimeoutContainer::ContainerType TimeoutContainer::removeTimeout(time_t timeoutTime)
{
    ContainerType timeoutSessions;
    auto it = mContainer.begin();
    while (it != mContainer.end())
    {
        assert((*it)->mpContainer == this);

        if ((*it)->time > timeoutTime)
        {
            break;
        }

        (*it)->mpContainer = nullptr;
        timeoutSessions.push_back(*it);

        auto delIt = it;
        ++it;
        mContainer.erase(delIt);
    }

    return move(timeoutSessions);
}

} // namespace mapper
