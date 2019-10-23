/**
 * @file service.h
 * @author Liu Yu (source@liuyu.com)
 * @brief Class for port-forward service
 * @version 1.0
 * @date 2019-10-11
 * 
 * @copyright Copyright (c) 2019
 * 
 */
#ifndef __MAPPER_SERVICE_H__
#define __MAPPER_SERVICE_H__

#include <string>
#include "buffer.hpp"
#include "define.h"
#include "sessionMgr.h"

namespace mapper
{

class Service
{
public:
    Service(int srcPort, std::string &targetAddress, int targetPort, SessionMgr &sessionMgr);
    virtual ~Service();

protected:
    int mSrcPort;
    std::string mTargetAddress;
    int mTargetPort;
    SessionMgr &mSessionMgr;
};

} // namespace mapper

#endif // __MAPPER_SERVICE_H__
