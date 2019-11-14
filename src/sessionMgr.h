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

#include <list>
#include "define.h"
#include "session.h"

namespace mapper
{

class SessionMgr
{
public:
    SessionMgr(int bufSize);
    ~SessionMgr();

    bool init(const int maxCount);
    void release();

    Session *alloc();
    void free(Session *pSession);

protected:
    int mBufSize;
    int mMaxCount;
    int mFreeCount;

    std::list<Session *> mFreeSessions;
};

} // namespace mapper

#endif // __MAPPER_SESSIONMGR_H__
