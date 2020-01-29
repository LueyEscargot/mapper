/**
 * @file tcpForwardService.h
 * @author Liu Yu (source@liuyu.com)
 * @brief TCP Service Class.
 * @version 1.0
 * @date 2020-01-05
 * 
 * @copyright Copyright (c) 2019
 * 
 */
#ifndef __MAPPER_LINK_TCPFORWARDSERVICE_H__
#define __MAPPER_LINK_TCPFORWARDSERVICE_H__

#include <list>
#include <memory>
#include <set>
#include <string>
#include <thread>
#include "forward.h"
#include "service.h"
#include "targetMgr.h"
#include "utils.h"
#include "../buffer/dynamicBuffer.h"
#include "../utils/timerList.h"

namespace mapper
{
namespace link
{

class TcpForwardService : public Service
{
protected:
    static const uint32_t EPOLL_THREAD_RETRY_INTERVAL;
    static const uint32_t EPOLL_MAX_EVENTS;
    static const uint32_t INTERVAL_EPOLL_WAIT_TIME;

protected:
    TcpForwardService(const TcpForwardService &) : Service(""){};
    TcpForwardService &operator=(const TcpForwardService &) { return *this; }

public:
    TcpForwardService();
    virtual ~TcpForwardService();

    bool init(std::list<std::shared_ptr<Forward>> &forwardList,
              Service::Setting_t &setting);

    void join() override;
    void stop() override;
    void close() override;
    void postProcess(time_t curTime);
    void scanTimeout(time_t curTime);

protected:
    void epollThread();
    bool initEnv();
    void closeEnv();
    bool doEpoll(int epollfd);
    void doTunnelSoc(time_t curTime, Endpoint_t *pe, uint32_t events);

    static void setStatus(Tunnel_t *pt, TunnelState_t stat);

    Tunnel_t *getTunnel();
    void acceptClient(time_t curTime, Endpoint_t *pe);
    bool connect(time_t curTime, Endpoint_t *pse, Tunnel_t *pt);

    void onRead(time_t curTime, int events, Endpoint_t *pe);
    void onWrite(time_t curTime, Endpoint_t *pe);

    inline void addToCloseList(Tunnel_t *pt) { mPostProcessList.insert(pt); };
    inline void addToCloseList(Endpoint_t *pe) { addToCloseList((Tunnel_t *)pe->container); }
    void closeTunnel(Tunnel_t *pt);
    void releaseEndpointBuffer(Endpoint_t *pe);

    inline void addToTimer(utils::TimerList &timer, time_t curTime, Tunnel_t *pt)
    {
        timer.push_back(curTime, &pt->timerEntity);
    }
    inline void removeFromTimer(utils::TimerList &timer, Tunnel_t *pt)
    {
        timer.erase(&pt->timerEntity);
    }
    inline void refreshTimer(utils::TimerList &timer, time_t curTime, Tunnel_t *pt)
    {
        timer.refresh(curTime, &pt->timerEntity);
    }
    void refreshTimer(time_t curTime, Tunnel_t *pt);
    inline void switchTimer(utils::TimerList &src, utils::TimerList &dst, time_t curTime, Tunnel_t *pt)
    {
        src.erase(&pt->timerEntity);
        dst.push_back(curTime, &pt->timerEntity);
    }

    static const bool StateMaine[TUNNEL_STATE_COUNT][TUNNEL_STATE_COUNT];

    int mEpollfd;
    volatile bool mStopFlag;
    std::thread mMainRoutineThread;

    std::list<std::shared_ptr<Forward>> mForwardList;
    Service::Setting_t mSetting;
    buffer::DynamicBuffer *mpDynamicBuffer;
    std::set<Tunnel_t *> mPostProcessList;
    std::set<Tunnel_t *> mCloseList;
    TargetManager mTargetManager;

    std::map<sockaddr_in, Endpoint_t *, Utils::Comparator_t> mAddr2ServiceEndpoint;
    std::set<Tunnel_t *> mTunnelList;

    utils::TimerList mConnectTimer;
    utils::TimerList mSessionTimer;
    utils::TimerList mReleaseTimer;
};

} // namespace link
} // namespace mapper

#endif // __MAPPER_LINK_TCPFORWARDSERVICE_H__
