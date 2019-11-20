#include "sessionMgr.h"
#include <assert.h>
#include <errno.h>
#include <string.h>
#include <exception>
#include <spdlog/spdlog.h>

using namespace std;

namespace mapper
{

SessionMgr::~SessionMgr()
{
    release();
}

bool SessionMgr::init(uint32_t bufSize, uint32_t maxCount)
{
    mMaxCount = maxCount;
    mFreeCount = maxCount;

    if ([&]() -> bool {
            for (int i = 0; i < maxCount; ++i)
            {
                Session *pSession;
                if (pSession = new Session(bufSize))
                {
                    mFreeSessions.push_back(pSession);
                }
                else
                {
                    return false;
                }
            }
            return true;
        }())
    {
        return true;
    }
    else
    {
        release();
        return false;
    }
}

void SessionMgr::release()
{
    for (auto *pSession : mFreeSessions)
    {
        SessionMgr::free(pSession);
    }
    mFreeSessions.clear();
}

Session *SessionMgr::alloc()
{
    if (mFreeSessions.empty())
    {
        spdlog::trace("[SessionMgr::alloc] no free session");
        return nullptr;
    }

    Session *pSession = mFreeSessions.front();
    mFreeSessions.pop_front();
    --mFreeCount;

    pSession->init();

    return pSession;
}

void SessionMgr::free(Session *pSession)
{
    assert(pSession->getContainer() == nullptr);
    if (!pSession)
    {
        return;
    }

    mFreeSessions.push_front(pSession);
    ++mFreeCount;
}

} // namespace mapper
