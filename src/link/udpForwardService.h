/**
 * @file udpForwardService.h
 * @author Liu Yu (source@liuyu.com)
 * @brief UDP Service Class.
 * @version 1.0
 * @date 2019-12-26
 * 
 * @copyright Copyright (c) 2019
 * 
 */
#ifndef __MAPPER_LINK_UDPFORWARDSERVICE_H__
#define __MAPPER_LINK_UDPFORWARDSERVICE_H__

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

class UdpForwardService : public Service
{
protected:
    static const uint32_t MAX_UDP_BUFFER = 1 << 16;
    static const uint32_t TIMEOUT_INTERVAL = 15;
    using Addr2TunIter = std::map<sockaddr_in, UdpTunnel_t *>::iterator;

    UdpForwardService(const UdpForwardService &) : Service("UdpForwardService"){};
    UdpForwardService &operator=(const UdpForwardService &) { return *this; }

public:
    UdpForwardService();
    virtual ~UdpForwardService();

    bool init(int epollfd,
              std::shared_ptr<config::Forward> forward,
              uint32_t sharedBufferCapacity);
    void close() override;
    void onSoc(time_t curTime, uint32_t events, Endpoint_t *pe) override;
    void postProcess(time_t curTime) override;
    void scanTimeout(time_t curTime) override;

    inline const Endpoint_t &getServiceEndpoint() const { return mServiceEndpoint; }

    void onServiceSoc(time_t curTime, uint32_t events, Endpoint_t *pe);
    void onNorthSoc(time_t curTime, uint32_t events, Endpoint_t *pe);

    inline void setTimeout(const uint32_t interval) { mTimeoutInterval = interval; };

protected:
    bool epollAddEndpoint(Endpoint_t *pe, bool read, bool write, bool edgeTriger);
    UdpTunnel_t *getTunnel(time_t curTime, sockaddr_in *pSAI);
    void southRead(time_t curTime, Endpoint_t *pe);
    void southWrite(time_t curTime, Endpoint_t *pe);
    void northRead(time_t curTime, Endpoint_t *pe);
    void northWrite(time_t curTime, Endpoint_t *pe);

    inline void addToCloseList(UdpTunnel_t *pt) { mCloseList.insert(pt); };
    inline void addToCloseList(Endpoint_t *pe) { addToCloseList((UdpTunnel_t *)pe->container); }
    void closeTunnels();
    void addToTimer(time_t curTime, TunnelTimer_t *p);
    void refreshTimer(time_t curTime, TunnelTimer_t *p);

    std::shared_ptr<config::Forward> mForwardCmd;
    buffer::DynamicBuffer *mpDynamicBuffer;
    Endpoint_t mServiceEndpoint;
    TargetManager mTargetManager;

    time_t mLastActionTime;
    uint32_t mTimeoutInterval;
    TunnelTimer_t mTimer; // 其中 next 指向第一个元素； prev 指向最后一个元素
    std::set<UdpTunnel_t *> mCloseList;

    std::map<sockaddr_in, UdpTunnel_t *, Utils::AddrCmp_t> mAddr2Tunnel;
    std::map<sockaddr_in, Endpoint_t *, Utils::AddrCmp_t> mAddr2Endpoint;
    std::map<int, sockaddr_in> mNorthSoc2SouthRemoteAddr;
};

} // namespace link
} // namespace mapper

#endif // __MAPPER_LINK_UDPFORWARDSERVICE_H__
