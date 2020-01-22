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
    TcpForwardService(const TcpForwardService &) : Service(""){};
    TcpForwardService &operator=(const TcpForwardService &) { return *this; }

public:
    TcpForwardService();
    virtual ~TcpForwardService();

    bool init(int epollfd,
              buffer::DynamicBuffer *pBuffer,
              std::shared_ptr<Forward> forward,
              Setting_t &setting);

    void close() override;
    void onSoc(time_t curTime, uint32_t events, Endpoint_t *pe) override;
    void postProcess(time_t curTime) override;
    void scanTimeout(time_t curTime) override;
    void processBufferWaitingList(time_t curTime) override;

protected:
    static void setStatus(Tunnel_t *pt, TunnelState_t stat);
    Tunnel_t *getTunnel();
    void acceptClient(time_t curTime, Endpoint_t *pe);
    bool connect(time_t curTime, Tunnel_t *pt);

    void onRead(time_t curTime, int events, Endpoint_t *pe);
    void onWrite(time_t curTime, Endpoint_t *pe);

    inline void addToCloseList(Tunnel_t *pt) { mPostProcessList.insert(pt); };
    inline void addToCloseList(Endpoint_t *pe) { addToCloseList((Tunnel_t *)pe->container); }
    void closeTunnel(Tunnel_t *pt);

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

    TargetManager mTargetManager;

    Setting_t mSetting;
    std::set<Tunnel_t *> mTunnelList;
    std::set<Tunnel_t *> mPostProcessList;

    utils::BaseList mBufferWaitList;
    utils::TimerList mConnectTimer;
    utils::TimerList mSessionTimer;
    utils::TimerList mReleaseTimer;
};

} // namespace link
} // namespace mapper

#endif // __MAPPER_LINK_TCPFORWARDSERVICE_H__
