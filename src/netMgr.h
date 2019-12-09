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
#include "endpoint.h"
#include "timeoutContainer.h"
#include "config/config.h"
#include "config/forward.h"
#include "link/type.h"
#include "link/tunnelMgr.h"
#include "timer/container.h"

namespace mapper
{

class NetMgr
{
    typedef union CONVERTER {
        uint32_t u32;
        void *ptr;
    } Converter_t;

public:
    static const int INTERVAL_EPOLL_RETRY;
    static const int INTERVAL_CONNECT_RETRY;
    static const int EPOLL_MAX_EVENTS = 16;
    static const int CONNECT_TIMEOUT = 3;
    static const int SESSION_TIMEOUT = 30;

    NetMgr();
    virtual ~NetMgr();

    bool start(config::Config &cfg);
    void stop();

    inline void join()
    {
        for (auto &t : mThreads)
            if (t.joinable())
                t.join();
    }

protected:
    void threadFunc();
    bool initEnv();
    void closeEnv();

    void onSoc(time_t curTime, epoll_event &event);
    void onService(time_t curTime, uint32_t events, link::EndpointService_t *pEndpoint);

    void acceptClient(time_t curTime, link::EndpointService_t *pes);
    bool epollAddTunnel(link::Tunnel_t *pt);
    void epollRemoveTunnel(link::Tunnel_t *pt);
    bool epollAddEndpoint(link::EndpointBase_t *pe, bool read, bool write, bool edgeTriger);
    void epollRemoveEndpoint(link::EndpointBase_t *pe);
    bool epollResetEndpointMode(link::EndpointBase_t *pe, bool read, bool write, bool edgeTriger);
    void postProcess(time_t curTime);
    void onClose(link::Tunnel_t *pt);

    void timeoutCheck(time_t curTime);

    std::vector<std::shared_ptr<mapper::config::Forward>> mForwards;
    std::vector<link::EndpointService_t *> mServices;

    config::Config * mpCfg;
    int mPreConnEpollfd;
    int mEpollfd;
    link::TunnelMgr mTunnelMgr;
    std::vector<std::thread> mThreads;
    bool mStopFlag;
    std::set<link::Tunnel_t *> mPostProcessList;

    timer::Container mConnectTimer;
    TimeoutContainer mTunnelTimer;
    uint32_t mConnectTimeout;
    uint32_t mSessionTimeout;
};

} // namespace mapper

#endif // __MAPPER_NETMGR_H__
