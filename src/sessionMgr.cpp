#include "sessionMgr.h"
#include <assert.h>
#include <errno.h>
#include <string.h>
#include <exception>
#include <spdlog/spdlog.h>

using namespace std;

namespace mapper
{

SessionMgr::SessionMgr(int bufSize)
    : mBufSize(bufSize),
      mMaxCount(0),
      mFreeCount(0)
{
}

SessionMgr::~SessionMgr()
{
    release();
}

bool SessionMgr::init(const int maxCount)
{
    mMaxCount = maxCount;
    mFreeCount = maxCount;

    if ([&]() -> bool {
            for (int i = 0; i < maxCount; ++i)
            {
                Session *pSession;
                if (pSession = new Session(mBufSize, 0, 0))
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

Session *SessionMgr::alloc(int northSoc, int southSoc)
{
    if (mFreeSessions.empty())
    {
        spdlog::trace("[SessionMgr::alloc] no free session");
        return nullptr;
    }

    Session *pSession = mFreeSessions.front();
    mFreeSessions.pop_front();
    --mFreeCount;

    pSession->init(northSoc, southSoc);
}

void SessionMgr::free(Session *pSession)
{
    if (!pSession)
    {
        return;
    }

    pSession->init(0, 0);

    mFreeSessions.push_front(pSession);
    ++mFreeCount;
}

} // namespace mapper
