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
#include <mutex>
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

class UdpForwardService : public Service
{
protected:
    static const uint32_t EPOLL_THREAD_RETRY_INTERVAL;
    static const uint32_t EPOLL_MAX_EVENTS;
    static const uint32_t INTERVAL_EPOLL_WAIT_TIME;
    static const uint32_t PREALLOC_RECV_BUFFER_SIZE;
    using Addr2TunIter = std::map<sockaddr_in, Tunnel_t *>::iterator;

    UdpForwardService(const UdpForwardService &) : Service(""){};
    UdpForwardService &operator=(const UdpForwardService &) { return *this; }

public:
    UdpForwardService();
    virtual ~UdpForwardService();

    bool init(std::list<std::shared_ptr<Forward>> &forwardList,
              Service::Setting_t &setting);

    void join() override;
    void stop() override;
    void close() override;
    std::string getStatistic(time_t curTime) override;
    void resetStatistic() override;

protected:
    void northThread();
    void southThread();
    bool initNorthEnv();
    bool initSouthEnv();
    void closeNorthEnv();
    void closeSouthEnv();
    void onTunnelSoc(time_t curTime, Endpoint_t *pe);
    bool doNorthEpoll(time_t curTime, int epollfd);
    bool doSouthEpoll(time_t curTime, int epollfd);
    void postProcess(time_t curTime);
    void scanTimeout(time_t curTime);

    Tunnel_t *getTunnel(time_t curTime, Endpoint_t *pse, sockaddr_in *pSAI);
    void southRead(time_t curTime, Endpoint_t *pse);
    void southWrite(time_t curTime, Endpoint_t *pe);
    void northRead(time_t curTime, Endpoint_t *pe);
    void northWrite(time_t curTime, Endpoint_t *pe);
    void processToNorthPkts(time_t curTime);
    void processToSouthPkts(time_t curTime);

    inline void addToCloseList(Tunnel_t *pt) { mCloseList.insert(pt); };
    inline void addToCloseList(Endpoint_t *pe)
    {
        addToCloseList((Tunnel_t *)pe->container);
    }
    void closeTunnels();
    void releaseEndpointBuffer(Endpoint_t *pe);

    int mServiceEpollfd;
    int mForwardEpollfd;
    std::thread mSouthThread;
    std::thread mNorthThread;
    volatile bool mStopFlag;

    std::mutex mAccessMutex;
    std::list<buffer::DynamicBuffer::BufBlk_t *> mToNorthPktList;
    std::list<buffer::DynamicBuffer::BufBlk_t *> mToSouthPktList;

    std::list<std::shared_ptr<Forward>> mForwardList;
    Service::Setting_t mSetting;
    buffer::DynamicBuffer *mpToNorthDynamicBuffer;
    buffer::DynamicBuffer *mpToSouthDynamicBuffer;
    std::set<Tunnel_t *> mCloseList;
    utils::TimerList mTimeoutTimer;
    TargetManager mTargetManager;

    std::map<sockaddr_in, Endpoint_t *, Utils::Comparator_t> mAddr2ServiceEndpoint;
    std::map<sockaddr_in, Tunnel_t *, Utils::Comparator_t> mAddr2Tunnel;
    std::map<int, Tunnel_t *> mSoc2Tunnel;

    // for statistic
    volatile float mUp;
    volatile float mDown;
    volatile float mTotalUp;
    volatile float mTotalDown;
};

} // namespace link
} // namespace mapper

#endif // __MAPPER_LINK_UDPFORWARDSERVICE_H__
