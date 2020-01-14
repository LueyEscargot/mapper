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
    void setTimeout(TunnelState_t stat, const uint32_t interval);

    void close() override;
    void onSoc(time_t curTime, uint32_t events, Endpoint_t *pe) override;
    void postProcess(time_t curTime) override;
    void scanTimeout(time_t curTime) override;

protected:
    static void setStatus(UdpTunnel_t *pt, TunnelState_t stat);
    UdpTunnel_t *getTunnel();
    void acceptClient(time_t curTime, Endpoint_t *pe);
    bool connect(time_t curTime, UdpTunnel_t *pt);

    bool epollAddEndpoint(Endpoint_t *pe, bool read, bool write, bool edgeTriger);
    bool epollResetEndpointMode(Endpoint_t *pe, bool read, bool write, bool edgeTriger);
    bool epollResetEndpointMode(UdpTunnel_t *pt, bool read, bool write, bool edgeTriger);
    void epollRemoveEndpoint(Endpoint_t *pe);
    void epollRemoveTunnel(UdpTunnel_t *pt);

    void onRead(time_t curTime, int events, Endpoint_t *pe);
    void onWrite(time_t curTime, Endpoint_t *pe);
    void appendToSendList(Endpoint_t *pe, buffer::DynamicBuffer::BufBlk_t *pBlk);

    inline void addToCloseList(UdpTunnel_t *pt) { mPostProcessList.insert(pt); };
    inline void addToCloseList(Endpoint_t *pe) { addToCloseList((UdpTunnel_t *)pe->container); }
    void closeTunnel(UdpTunnel_t *pt);

    void addToTimer(time_t curTime, TunnelTimer_t *p);
    void refreshTimer(time_t curTime, TunnelTimer_t *p);

    // void addToBufferWaitingList(time_t curTime, Endpoint_t *pe);
    // void removeFromWaitingList(Endpoint_t *pe);
    void processBufferWaitingList();
    // inline bool isInBufferWaitingList(Endpoint_t *pe)
    // {
    //     return pe->waitBufferPrev ||
    //            pe->waitBufferNext ||
    //            mBufferWaitList.waitBufferNext == pe;
    // }

    static const bool StateMaine[TUNNEL_STATE_COUNT][TUNNEL_STATE_COUNT];

    TargetManager mTargetManager;

    Setting_t mSetting;
    TunnelTimer_t mTimer; // 其中 next 指向第一个元素； prev 指向最后一个元素
    std::set<UdpTunnel_t *> mTunnelList;
    std::set<UdpTunnel_t *> mPostProcessList;

    utils::TimerList mBufferWaitList;
};

} // namespace link
} // namespace mapper

#endif // __MAPPER_LINK_TCPFORWARDSERVICE_H__
