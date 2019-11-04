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
      mMaxSessions(0),
      mFreeSessions(0),
      mpMemBlock(nullptr),
      mpMemBlockEndPos(nullptr)
    //   ,
    //   mpFreeItems(nullptr)
{
}

SessionMgr::~SessionMgr()
{
    release();
}

bool SessionMgr::init(const int maxSessions)
{
    // if (mpFreeItems)
    // {
    //     spdlog::error("[SessionMgr::init] session buffer list not empty!");
    //     return false;
    // }

    // // alloc memory
    // auto entrySize = sizeof(Session_t);
    // auto reqSize = entrySize * maxSessions;
    // mpMemBlock = malloc(reqSize);
    // if (!mpMemBlock)
    // {
    //     spdlog::error("[SessionMgr::init] alloc [{}] bytes fail: {} - {}",
    //                   reqSize, errno, strerror(errno));
    //     return false;
    // }
    // mpMemBlockEndPos = static_cast<char *>(mpMemBlock) + reqSize;
    // spdlog::debug("[SessionMgr::init] alloc[{} x {} = {}] bytes at blk[ {} - {} ]",
    //               entrySize, maxSessions, reqSize, mpMemBlock, mpMemBlockEndPos);

    // // init free items
    // mpFreeItems = static_cast<FreeItem_t *>(mpMemBlock);
    // FreeItem_t *pItem = mpFreeItems;
    // FreeItem_t *pNext = pItem + 1;
    // for (int i = 0; i < maxSessions - 1; ++i, ++pItem, ++pNext)
    // {
    //     pItem->next = pNext;
    // }
    // pItem->next = nullptr;

    mMaxSessions = maxSessions;
    mFreeSessions = maxSessions;

    return true;
}

void SessionMgr::release()
{
    assert(isFree());

    // // release mpMemBlock
    // if (mpMemBlock)
    // {
    //     ::free(mpMemBlock);
    //     mpMemBlock = nullptr;
    //     mpFreeItems = nullptr;
    //     mMaxSessions = 0;
    //     mFreeSessions = 0;
    // }
}

// Session_t *SessionMgr::allocSession()
// {
//     if (mFreeSessions == 0)
//     {
//         return nullptr;
//     }

//     FreeItem_t *pFreeObj = mpFreeItems;
//     mpFreeItems = pFreeObj->next;
//     --mFreeSessions;
//     assert(pFreeObj);
//     assert(mFreeSessions >= 0);

//     pFreeObj->session.init();
//     return &pFreeObj->session;
// }

Session *SessionMgr::alloc(int northSoc, int southSoc)
{
    if (mFreeSessions == 0)
    {
        spdlog::trace("[SessionMgr::alloc] no free session");
        return nullptr;
    }

    try
    {
        --mFreeSessions;
        return new Session(mBufSize, northSoc, southSoc);
    }
    catch (exception &e)
    {
        spdlog::error("[SessionMgr::alloc] catch an exception: {}", e.what());
        ++mFreeSessions;
        return nullptr;
    }
}

// void SessionMgr::freeSession(Session_t *pSession)
// {
//     if (!pSession)
//     {
//         return;
//     }

//     assert(mpMemBlock <= pSession);
//     assert(pSession < mpMemBlockEndPos);

//     FreeItem_t *pFreeObj = reinterpret_cast<FreeItem_t *>(pSession);
//     pFreeObj->next = mpFreeItems;
//     mpFreeItems = pFreeObj;
//     ++mFreeSessions;
//     assert(mFreeSessions <= mMaxSessions);
// }

void SessionMgr::free(Session *pSession)
{
    if (!pSession)
    {
        return;
    }

    delete pSession;
    ++mFreeSessions;
}

} // namespace mapper
