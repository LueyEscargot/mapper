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
#include <thread>
#include <vector>
#include "define.h"
#include "endpoint.h"
#include "sessionMgr.h"
#include "timeoutContainer.h"

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
    static const int CONNECT_TIMEOUT = 10;
    static const int SESSION_TIMEOUT = 30;

    NetMgr(uint32_t bufSize);
    virtual ~NetMgr();

    bool start(const int maxSessions, std::vector<mapper::MapData_t> &mapDatas);
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
    void onService(time_t curTime, uint32_t events, Endpoint *pEndpoint);

    void acceptClient(time_t curTime, Endpoint *pEndpoint);
    int createNorthSoc(MapData_t *pMapData);
    void postProcess(time_t curTime);
    void onClose(Session *pSession);

    void removeAndCloseSoc(int sock);
    bool joinEpoll(Endpoint *pEndpoint, bool read, bool write);
    bool resetEpollMode(Endpoint *pEndpoint, bool read, bool write);

    void *serviceIndexToPtr(uint32_t index);
    uint32_t ptrToServiceIndex(void *p);
    void onSessionStatues(Session * pSession);

    std::vector<mapper::MapData_t> mMapDatas;

    int mEpollfd;
    SessionMgr mSessionMgr;
    std::vector<std::shared_ptr<Endpoint>> mSvrEndpoints;
    std::vector<std::thread> mThreads;
    bool mStopFlag;
    std::list<Session*> mPostProcessList;

    TimeoutContainer mConnectTimeoutContainer;
    TimeoutContainer mSessionTimeoutContainer;
};

} // namespace mapper

#endif // __MAPPER_NETMGR_H__
