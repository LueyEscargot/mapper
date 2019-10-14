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

#include "define.h"

namespace mapper
{

class SessionMgr
{
public:
    SessionMgr();
    ~SessionMgr();

    bool init(const int maxSessions);
    void release();
    inline bool isFree() { return mFreeSessions == mMaxSessions; }

    Session_t *allocSession();
    void freeSession(void *pSession);

protected:
    int mMaxSessions;
    int mFreeSessions;
    void *mpMemBlock;
    void *mpMemBlockEndPos;
    FreeItem_t *mpFreeItems;
};

} // namespace mapper

#endif // __MAPPER_SESSIONMGR_H__
