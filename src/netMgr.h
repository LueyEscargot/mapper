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
#include <thread>
#include <vector>
#include "define.h"
#include "sessionMgr.h"

namespace mapper
{

class NetMgr
{
public:
    static const int INTERVAL_EPOLL_RETRY;
    static const int INTERVAL_CONNECT_RETRY;
    static const int EPOLL_MAX_EVENTS = 16;

    NetMgr();
    virtual ~NetMgr();

    bool start(const int maxSessions, std::vector<mapper::MapData_t> *pMapDatas);
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

    void onSoc(int64_t curTime, epoll_event &event);
    void onSvrSoc(int64_t curTime, uint32_t events, SockSvr_t *pSoc);
    void onHostSoc(int64_t curTime, uint32_t events, SockHost_t *pSoc);
    void onClientSoc(int64_t curTime, uint32_t events, SockClient_t *pSoc);

    void acceptClient(SockSvr_t *pSoc);
    int createHostSoc(SockSvr_t *pSoc);
    void postProcess(int64_t curTime);
    void onFail(Session_t *pSession);
    void removeAndCloseSoc(int sock);

    std::vector<mapper::MapData_t> *mpMapDatas;

    int mEpollfd;
    SessionMgr mSessionMgr;
    std::vector<SockSvr_t *> mpSvrSocs;
    std::vector<std::thread> mThreads;
    bool mStopFlag;
};

} // namespace mapper

#endif // __MAPPER_NETMGR_H__
