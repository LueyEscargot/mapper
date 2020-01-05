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

#include <set>
#include "service.h"
#include "targetMgr.h"
#include "utils.h"
#include "../buffer/dynamicBuffer.h"
#include "../config/forward.h"

namespace mapper
{
namespace link
{

class TcpForwardService : public Service
{
protected:
    static const uint32_t DEFAULT_RECV_BUFFER = 1 << 16;
    static const uint32_t TIMEOUT_INTERVAL_CONN = 15;
    static const uint32_t TIMEOUT_INTERVAL_ESTB = 180;
    static const uint32_t TIMEOUT_INTERVAL_BROK = 15;

    TcpForwardService(const TcpForwardService &) : Service(""){};
    TcpForwardService &operator=(const TcpForwardService &) { return *this; }

public:
    TcpForwardService();
    virtual ~TcpForwardService();

    bool init(int epollfd,
              std::shared_ptr<config::Forward> forward,
              uint32_t sharedBufferCapacity);
    void setTimeout(TunnelState_t stat, const uint32_t interval);

    inline const Endpoint_t &getServiceEndpoint() const { return mServiceEndpoint; }

    void close() override;
    void onSoc(time_t curTime, uint32_t events, Endpoint_t *pe) override;

    void acceptClient(time_t curTime, Endpoint_t *pe);
    void onServiceSoc(time_t curTime, uint32_t events, Endpoint_t *pe);
    void onNorthSoc(time_t curTime, uint32_t events, Endpoint_t *pe);

protected:
    static void setStatus(UdpTunnel_t *pt, TunnelState_t stat);
    UdpTunnel_t *getTunnel();
    bool connect(time_t curTime, UdpTunnel_t *pt);

    bool epollAddEndpoint(Endpoint_t *pe, bool read, bool write, bool edgeTriger);
    UdpTunnel_t *getTunnel(time_t curTime, sockaddr_in *sa);
    void southRead(time_t curTime, Endpoint_t *pe);
    void southWrite(time_t curTime, Endpoint_t *pe);
    void northRead(time_t curTime, Endpoint_t *pe);
    void northWrite(time_t curTime, Endpoint_t *pe);

    inline void addToCloseList(UdpTunnel_t *pt) { mCloseList.insert(pt); };
    inline void addToCloseList(Endpoint_t *pe) { addToCloseList((UdpTunnel_t *)pe->container); }
    void closeTunnels();
    void addToTimer(time_t curTime, TunnelTimer_t *p);
    void refreshTimer(time_t curTime, TunnelTimer_t *p);
    void scanTimeout(time_t curTime);

    static const bool StateMaine[TUNNEL_STATE_COUNT][TUNNEL_STATE_COUNT];

    std::shared_ptr<config::Forward> mForwardCmd;
    buffer::DynamicBuffer *mpDynamicBuffer;
    Endpoint_t mServiceEndpoint;
    TargetManager mTargetManager;

    time_t mLastActionTime;
    uint32_t mTimeoutInterval_Conn;
    uint32_t mTimeoutInterval_Estb;
    uint32_t mTimeoutInterval_Brok;
    TunnelTimer_t mTimer; // 其中 next 指向第一个元素； prev 指向最后一个元素
    std::set<UdpTunnel_t *> mTunnelList;
    std::set<UdpTunnel_t *> mCloseList;
};

} // namespace link
} // namespace mapper

#endif // __MAPPER_LINK_TCPFORWARDSERVICE_H__
