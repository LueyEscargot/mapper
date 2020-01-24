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

#include <list>
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

class UdpForwardService : public Service
{
protected:
    static const uint32_t PREALLOC_RECV_BUFFER_SIZE = 1 << 16;
    using Addr2TunIter = std::map<sockaddr_in, Tunnel_t *>::iterator;

    UdpForwardService(const UdpForwardService &) : Service("UdpForwardService"){};
    UdpForwardService &operator=(const UdpForwardService &) { return *this; }

public:
    UdpForwardService();
    virtual ~UdpForwardService();

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
    Tunnel_t *getTunnel(time_t curTime, sockaddr_in *pSAI);
    void southRead(time_t curTime, Endpoint_t *pe);
    Tunnel_t *southRead(time_t curTime, Endpoint_t *pe, char *buffer);
    void southWrite(time_t curTime, Endpoint_t *pe);
    void northRead(time_t curTime, Endpoint_t *pe);
    Tunnel_t *northRead(time_t curTime, Endpoint_t *pe, char *buffer);
    void northWrite(time_t curTime, Endpoint_t *pe);

    inline void addToCloseList(Tunnel_t *pt) { mCloseList.insert(pt); };
    inline void addToCloseList(Endpoint_t *pe) { addToCloseList((Tunnel_t *)pe->container); }
    void closeTunnels();

    int mServiceEpollfd;
    int mForwardEpollfd;

    std::shared_ptr<Forward> mForwardCmd;
    TargetManager mTargetManager;

    time_t mLastActionTime;
    Setting_t mSetting;
    std::set<Tunnel_t *> mCloseList;
    utils::TimerList mTimeoutTimer;
    utils::BaseList mBufferWaitList;

    std::map<sockaddr_in, Tunnel_t *, Utils::Comparator_t> mAddr2Tunnel;
    std::map<int, sockaddr_in> mSoc2SouthRemoteAddr;
    std::map<int, Tunnel_t *> mSoc2Tunnel;
};

} // namespace link
} // namespace mapper

#endif // __MAPPER_LINK_UDPFORWARDSERVICE_H__
