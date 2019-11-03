/**
 * @file sessionMgr.h
 * @author Liu Yu(source@liuyu.com)
 * @brief Session Manager
 * @version 1.0
 * @date 2019-10-07
 * 
 * @copyright Copyright (c) 2019
 * 
 */

#ifndef __MAPPER_SESSIONMGR_H__
#define __MAPPER_SESSIONMGR_H__

#include <memory>
#include "define.h"
#include "session.h"

namespace mapper
{

class SessionMgr
{
public:
    SessionMgr(int bufSize);
    ~SessionMgr();

    bool init(const int maxSessions);
    void release();
    inline bool isFree() { return mFreeSessions == mMaxSessions; }

    Session_t *allocSession();
    void freeSession(Session_t *pSession);
    Session *alloc(int northSoc, int southSoc);
    void free(Session *pSession);

protected:
    int mBufSize;
    int mMaxSessions;
    int mFreeSessions;
    void *mpMemBlock;
    void *mpMemBlockEndPos;
    FreeItem_t *mpFreeItems;
};

} // namespace mapper

#endif // __MAPPER_SESSIONMGR_H__
