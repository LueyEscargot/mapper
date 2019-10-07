/**
 * @file mapper.h
 * @author Liu Yu (source@liuyu.com)
 * @brief Project main service routing.
 * @version 1.0
 * @date 2019-10-07
 * 
 * @copyright Copyright (c) 2019
 * 
 */

#ifndef __MAPPER_MAPPER_H__
#define __MAPPER_MAPPER_H__

#include "sessionMgr.h"

namespace mapper
{

class Mapper
{
public:
    Mapper();
    ~Mapper();

    bool run(const int maxSessions);

protected:
    bool init(const int maxSessions);
    void release();

    SessionMgr mSessionMgr;
};

} // namespace mapper

#endif // __MAPPER_MAPPER_H__
