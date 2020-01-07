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
#include <vector>
#include "config/config.h"
#include "config/forward.h"
#include "link/service.h"
#include "link/type.h"

namespace mapper
{

class NetMgr
{
public:
    static const int INTERVAL_EPOLL_RETRY;
    static const int INTERVAL_CONNECT_RETRY;
    static const int EPOLL_MAX_EVENTS;

    NetMgr();
    virtual ~NetMgr();

    bool start(config::Config &cfg);
    void stop();

    void join();

protected:
    void threadFunc();
    bool initEnv();
    void closeEnv();

    std::vector<std::shared_ptr<mapper::config::Forward>> mForwards;
    std::vector<link::Service *> mServices;

    config::Config *mpCfg;
    int mEpollfd;
    std::thread mMainRoutineThread;
    volatile bool mStopFlag;
};

} // namespace mapper

#endif // __MAPPER_NETMGR_H__
