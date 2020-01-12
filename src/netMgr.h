/**
 * @file netMgr.h
 * @author Liu Yu (source@liuyu.com)
 * @brief Network events manager.
 * @version 1.0
 * @date 2019-10-08
 * 
 * @copyright Copyright (c) 2019
 * 
 */

#ifndef __MAPPER_NETMGR_H__
#define __MAPPER_NETMGR_H__

#include <sys/epoll.h>
#include <string.h>
#include <time.h>
#include <list>
#include <memory>
#include <set>
#include <thread>
#include <rapidjson/document.h>
#include "buffer/dynamicBuffer.h"
#include "link/service.h"
#include "link/type.h"

namespace mapper
{

class NetMgr
{
public:
    static const uint32_t INTERVAL_EPOLL_RETRY;
    static const uint32_t INTERVAL_CONNECT_RETRY;
    static const uint32_t EPOLL_MAX_EVENTS;

    static const uint32_t DEFAULT_BUFFER_SIZE = 128;
    static const uint32_t BUFFER_SIZE_UNIT = 1048576;
    static const char *SETTING_BUFFER_SIZE_PATH;

    NetMgr();
    virtual ~NetMgr();

    bool start(rapidjson::Document &cfg);
    void stop();

    void join();

protected:
    void threadFunc();
    bool initEnv();
    void closeEnv();

    std::list<link::Service *> mServiceList;

    rapidjson::Document *mpCfg;
    int mEpollfd;
    std::thread mMainRoutineThread;
    volatile bool mStopFlag;

    buffer::DynamicBuffer *mpDynamicBuffer;
};

} // namespace mapper

#endif // __MAPPER_NETMGR_H__
