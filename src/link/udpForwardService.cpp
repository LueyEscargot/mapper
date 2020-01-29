#include "udpForwardService.h"
#include <execinfo.h>
#include <time.h>
#include <sys/epoll.h>
#include <sstream>
#include <rapidjson/document.h>
#include <spdlog/spdlog.h>
#include "endpoint.h"
#include "tunnel.h"
#include "utils.h"
#include "../utils/jsonUtils.h"

#include "schema.def"

using namespace std;
using namespace rapidjson;
using namespace mapper::buffer;
using namespace mapper::utils;

namespace mapper
{
namespace link
{

const uint32_t UdpForwardService::EPOLL_THREAD_RETRY_INTERVAL = 7;
const uint32_t UdpForwardService::EPOLL_MAX_EVENTS = 8;
const uint32_t UdpForwardService::INTERVAL_EPOLL_WAIT_TIME = 50;
const uint32_t UdpForwardService::PREALLOC_RECV_BUFFER_SIZE = 1 << 16;

UdpForwardService::UdpForwardService()
    : Service("UdpForwardService"),
      mServiceEpollfd(0),
      mForwardEpollfd(0),
      mStopFlag(false)
{
}

UdpForwardService::~UdpForwardService()
{
    closeTunnels();
}

bool UdpForwardService::init(list<shared_ptr<Forward>> &forwardList, Setting_t &setting)
{
    spdlog::debug("[UdpForwardService::init] init udp forward service");

    // check existed thread
    if (mMainRoutineThread.joinable())
    {
        spdlog::warn("[UdpForwardService::init] forward service thread not stop.");
        return true;
    }
    mStopFlag = false;

    mSetting = setting;
    mForwardList.swap(forwardList);

    // create buffer
    spdlog::trace("[UdpForwardService::init] create buffer");
    mpDynamicBuffer = buffer::DynamicBuffer::allocDynamicBuffer(setting.bufferSize);
    if (!mpDynamicBuffer)
    {
        spdlog::error("[UdpForwardService::init] alloc buffer fail");
        return false;
    }

    // start thread
    spdlog::trace("[UdpForwardService::init] start thread");
    mMainRoutineThread = thread(&UdpForwardService::epollThread, this);

    return true;
}

void UdpForwardService::join()
{
    mMainRoutineThread.joinable() && (mMainRoutineThread.join(), true);
}

void UdpForwardService::stop()
{
    // set stop flag
    spdlog::trace("[UdpForwardService::stop] set stop flag");
    mStopFlag = true;
}

void UdpForwardService::close()
{
    spdlog::debug("[UdpForwardService::close] close udp forward service");

    // stop thread
    spdlog::trace("[UdpForwardService::close] stop thread");
    mStopFlag = true;
    join();

    // release buffer
    spdlog::trace("[UdpForwardService::close] release buffer");
    mpDynamicBuffer && (DynamicBuffer::releaseDynamicBuffer(mpDynamicBuffer), mpDynamicBuffer = nullptr);
}

void UdpForwardService::epollThread()
{
    spdlog::debug("[UdpForwardService::epollThread] udp forward service thread start");

    while (!mStopFlag)
    {
        // init env
        spdlog::debug("[UdpForwardService::epollThread] init env");
        if (!initEnv())
        {
            spdlog::error("[UdpForwardService::epollThread] init fail. wait {} seconds",
                          EPOLL_THREAD_RETRY_INTERVAL);
            closeEnv();
            this_thread::sleep_for(chrono::seconds(EPOLL_THREAD_RETRY_INTERVAL));
            continue;
        }

        // main routine
        try
        {
            time_t lastScanTime = 0;
            time_t curTime;

            while (!mStopFlag)
            {
                curTime = time(nullptr);

                if (!doSouthEpoll(curTime, mServiceEpollfd) || // for service soc
                    !doNorthEpoll(curTime, mForwardEpollfd))   // for north soc
                {
                    spdlog::error("[UdpForwardService::epollThread] do epoll fail.");
                    break;
                }

                // post process
                postProcess(curTime);

                // scan timeout
                if (lastScanTime < curTime)
                {
                    scanTimeout(curTime);
                    lastScanTime = curTime;
                }
            }
        }
        catch (const exception &e)
        {
            static const uint32_t BACKTRACE_BUFFER_SIZE = 128;
            void *buffer[BACKTRACE_BUFFER_SIZE];
            char **strings;

            size_t addrNum = backtrace(buffer, BACKTRACE_BUFFER_SIZE);
            strings = backtrace_symbols(buffer, addrNum);

            spdlog::error("[UdpForwardService::epollThread] catch an exception. {}", e.what());
            if (strings == nullptr)
            {
                spdlog::error("[UdpForwardService::epollThread] backtrace_symbols fail.");
            }
            else
            {
                for (int i = 0; i < addrNum; i++)
                    spdlog::error("[UdpForwardService::epollThread] {}", strings[i]);
                free(strings);
            }
        }

        // close env
        closeEnv();

        if (!mStopFlag)
        {
            spdlog::debug("[UdpForwardService::epollThread] sleep {} secnds and try again", EPOLL_THREAD_RETRY_INTERVAL);
            this_thread::sleep_for(chrono::seconds(EPOLL_THREAD_RETRY_INTERVAL));
        }
    }

    spdlog::debug("[UdpForwardService::epollThread] udp forward service thread stop");
}

bool UdpForwardService::initEnv()
{
    // init epoll fds
    spdlog::trace("[UdpForwardService::initEnv] init epoll fds");
    if ((mServiceEpollfd = epoll_create1(0)) < 0 ||
        (mForwardEpollfd = epoll_create1(0)) < 0)
    {
        spdlog::error("[UdpForwardService::initEnv] Failed to create epolls. {} - {}",
                      errno, strerror(errno));
        return false;
    }

    // init udp forward services
    spdlog::trace("[UdpForwardService::initEnv] init udp forward services");
    for (auto &forward : mForwardList)
    {
        // get service address of specified interface and port
        spdlog::trace("[UdpForwardService::initEnv] get service address of specified interface and port");
        sockaddr_in sai;
        if (!Utils::getIntfAddr(forward->interface.c_str(), sai))
        {
            spdlog::error("[UdpForwardService::initEnv] get address of interface[{}] fail.", forward->interface);
            return false;
        }
        sai.sin_port = htons(atoi(forward->service.c_str()));

        // service 是否已经存在
        Endpoint_t *pe;
        auto it = mAddr2ServiceEndpoint.find(sai);
        if (it == mAddr2ServiceEndpoint.end())
        {
            // 新服务

            // create service endpoint
            spdlog::trace("[UdpForwardService::initEnv] create service endpoint");
            pe = Endpoint::getEndpoint(PROTOCOL_UDP, TO_SOUTH, TYPE_SERVICE);
            if (!pe)
            {
                spdlog::error("[UdpForwardService::initEnv] create service endpoint fail.");
                return false;
            }

            // create service soc
            spdlog::trace("[UdpForwardService::initEnv] create service soc");
            pe->soc = Utils::createServiceSoc(PROTOCOL_UDP, &sai, sizeof(sockaddr_in));
            if (pe->soc > 0)
            {
                pe->conn.localAddr = sai;
                mAddr2ServiceEndpoint[sai] = pe;
            }
            else
            {
                Endpoint::releaseEndpoint(pe);
                spdlog::error("[UdpForwardService::initEnv] create service soc for {}:{} fail.",
                              forward->interface, forward->service);
                return false;
            }

            // add service endpoint into epoll driver
            spdlog::trace("[UdpForwardService::initEnv] add service endpoint into epoll driver");
            if (!epollAddEndpoint(mServiceEpollfd, pe, true, true, false))
            {
                spdlog::error("[UdpForwardService::init] add service endpoint into epoll driver fail.");
                return false;
            }

            spdlog::trace("[UdpForwardService::initEnv] create udp forward service: {}",
                          Utils::dumpSockAddr(pe->conn.localAddr));
        }
        else
        {
            pe = it->second;
        }
        if (mTargetManager.addTarget(pe->soc,
                                     forward->targetHost.c_str(),
                                     forward->targetService.c_str(),
                                     PROTOCOL_UDP))
        {
            spdlog::trace("[UdpForwardService::initEnv] add target: {} -> {}:{}",
                          Utils::dumpSockAddr(pe->conn.localAddr),
                          forward->interface, forward->service);
        }
        else
        {
            spdlog::error("[UdpForwardService::initEnv] get target[{}:{}] into target manager fail.",
                          forward->interface, forward->service);
            return false;
        }
    }

    return true;
}

void UdpForwardService::closeEnv()
{
    // close udp forward services
    if (!mAddr2ServiceEndpoint.empty())
    {
        spdlog::trace("[UdpForwardService::closeEnv] close udp forward services");
        for (auto it : mAddr2ServiceEndpoint)
        {
            spdlog::trace("[UdpForwardService::closeEnv] close udp forward service: {}",
                          Utils::dumpSockAddr(it.second->conn.localAddr));

            // close socket
            it.second->soc && (::close(it.second->soc), it.second->soc = 0);
            // release endpoint_t object
            Endpoint::releaseEndpoint(it.second);
        }
        mAddr2ServiceEndpoint.clear();
    }

    // clean target manager
    mTargetManager.clear();

    // close epoll fd
    spdlog::trace("[UdpForwardService::closeEnv] close epoll fds");
    mServiceEpollfd && (::close(mServiceEpollfd), mServiceEpollfd = 0);
    mForwardEpollfd && (::close(mForwardEpollfd), mForwardEpollfd = 0);
}

bool UdpForwardService::doNorthEpoll(time_t curTime, int epollfd)
{
    struct epoll_event ee[EPOLL_MAX_EVENTS];

    int nRet = epoll_wait(epollfd, ee, EPOLL_MAX_EVENTS, INTERVAL_EPOLL_WAIT_TIME);
    if (nRet > 0)
    {
        for (int i = 0; i < nRet; ++i)
        {
            link::Endpoint_t *pe = (link::Endpoint_t *)ee[i].data.ptr;

            // Write
            if (ee[i].events & EPOLLOUT)
            {
                northWrite(curTime, pe);
            }

            // Read
            if (ee[i].events & EPOLLIN)
            {
                northRead(curTime, pe);
            }
        }
    }
    else if (nRet < 0)
    {
        if (errno != EAGAIN && errno != EINTR)
        {
            spdlog::error("[UdpForwardService::doNorthEpoll] epoll fail. {} - {}",
                          errno, strerror(errno));
            return false;
        }
    }

    return true;
}

bool UdpForwardService::doSouthEpoll(time_t curTime, int epollfd)
{
    struct epoll_event ee[EPOLL_MAX_EVENTS];

    int nRet = epoll_wait(epollfd, ee, EPOLL_MAX_EVENTS, INTERVAL_EPOLL_WAIT_TIME);
    if (nRet > 0)
    {
        for (int i = 0; i < nRet; ++i)
        {
            link::Endpoint_t *pe = (link::Endpoint_t *)ee[i].data.ptr;

            // Write
            if (ee[i].events & EPOLLOUT)
            {
                southWrite(curTime, pe);
            }

            // Read
            if (ee[i].events & EPOLLIN)
            {
                southRead(curTime, pe);
            }
        }
    }
    else if (nRet < 0)
    {
        if (errno != EAGAIN && errno != EINTR)
        {
            spdlog::error("[UdpForwardService::doSouthEpoll] epoll fail. {} - {}",
                          errno, strerror(errno));
            return false;
        }
    }

    return true;
}

void UdpForwardService::postProcess(time_t curTime)
{
    // clean useless tunnels
    closeTunnels();
}

void UdpForwardService::scanTimeout(time_t curTime)
{
    // TODO: nothin in timeout timer

    time_t timeoutTime = curTime - mSetting.udpTimeout;
    list<TimerList::Entity_t *> timeoutList;
    mTimeoutTimer.getTimeoutList(timeoutTime, timeoutList);
    for (auto entity : timeoutList)
    {
        auto pt = (Tunnel_t *)entity;
        spdlog::trace("[UdpForwardService::scanTimeout] tunnel[{}] timeout", pt->north->soc);
        addToCloseList(pt);
    }
}

Tunnel_t *UdpForwardService::getTunnel(time_t curTime, Endpoint_t *pse, sockaddr_in *southRemoteAddr)
{
    // 从已缓存 tunnel 中查找
    auto it = mAddr2Tunnel.find(*southRemoteAddr);
    if (it != mAddr2Tunnel.end())
    {
        return it->second;
    }

    // create north endpoint
    auto north = Endpoint::getEndpoint(PROTOCOL_UDP, TO_NORTH, TYPE_NORMAL);
    if (north == nullptr)
    {
        spdlog::error("[UdpForwardService::getTunnel] create north endpoint fail");
        return nullptr;
    }
    else
    {
        north->service = this;
        north->peer = pse;

        // create to north socket
        north->soc = Utils::createSoc(PROTOCOL_UDP, true);
        if (north->soc <= 0)
        {
            spdlog::error("[UdpForwardService::getTunnel] create north socket fail.");
            Endpoint::releaseEndpoint(north);
            return nullptr;
        }
        // spdlog::debug("[UdpForwardService::getTunnel] create north socket[{}].", north->soc);

        // connect to host
        auto addr = mTargetManager.getAddr(pse->soc);
        if ([&]() {
                if (!addr)
                {
                    spdlog::error("[UdpForwardService::getTunnel] connect to north host fail.");
                    return false;
                }
                else if (connect(north->soc, (const sockaddr *)addr, sizeof(sockaddr_in)) < 0)
                {
                    // report fail
                    mTargetManager.failReport(curTime, addr);
                    spdlog::error("[UdpForwardService::getTunnel] connect fail. {} - {}",
                                  errno, strerror(errno));
                    return false;
                }

                // add into epoll driver
                if (!epollAddEndpoint(mForwardEpollfd, north, true, true, false))
                {
                    spdlog::error("[UdpForwardService::getTunnel] add endpoint[{}] into epoll fail.", north->soc);
                    return false;
                }

                return true;
            }())
        {
            // save ip-tuple info
            socklen_t socLen;
            getsockname(north->soc, (sockaddr *)&north->conn.localAddr, &socLen);
            north->conn.remoteAddr = *addr;
        }
        else
        {
            ::close(north->soc);
            Endpoint::releaseEndpoint(north);
            return nullptr;
        }
    }

    // create tunnel
    auto pt = Tunnel::getTunnel();
    if (pt == nullptr)
    {
        spdlog::error("[UdpForwardService::getTunnel] create tunnel fail");
        ::close(north->soc);
        Endpoint::releaseEndpoint(north);
        return nullptr;
    }

    // bind tunnel and endpoints
    pt->north = north;
    pt->south = pse;
    north->container = pt;

    // put into map
    // spdlog::trace("[UdpForwardService::getTunnel] put addr[{}] into map",
    //               Utils::dumpSockAddr(southRemoteAddr));
    mAddr2Tunnel[*southRemoteAddr] = pt;
    mSoc2SouthRemoteAddr[north->soc] = *southRemoteAddr;
    mSoc2Tunnel[north->soc] = pt;

    // add to timer
    mTimeoutTimer.push_back(curTime, &pt->timerEntity);

    spdlog::debug("[UdpForwardService::getTunnel] create tunnel[{}]: {}=>{}=>{}",
                  north->soc,
                  Utils::dumpSockAddr(southRemoteAddr),
                  Utils::dumpSockAddr(pse->conn.localAddr),
                  Utils::dumpSockAddr(north->conn.remoteAddr));

    return pt;
}

void UdpForwardService::southRead(time_t curTime, Endpoint_t *pse)
{
    if (!pse->valid)
    {
        spdlog::critical("[UdpForwardService::southRead] service soc[{}] not valid");
        return;
    }

    while (true)
    {
        // 按最大 UDP 数据包预申请内存
        auto pBuf = mpDynamicBuffer->reserve(PREALLOC_RECV_BUFFER_SIZE);
        if (pBuf == nullptr)
        {
            // out of buffer
            spdlog::trace("[UdpForwardService::southRead] out of buffer, drop packet");
            static char buffer[16];
            recvfrom(pse->soc, buffer, 16, 0, nullptr, 0);
            break;
        }

        if (!southRead(curTime, pse, pBuf))
        {
            // 此次接收操作已完成
            break;
        }
    }
}

bool UdpForwardService::southRead(time_t curTime, Endpoint_t *pse, char *buffer)
{
    sockaddr_in addr;
    socklen_t addrLen = sizeof(sockaddr_in);
    int nRet = recvfrom(pse->soc,
                        buffer,
                        PREALLOC_RECV_BUFFER_SIZE,
                        0,
                        (sockaddr *)&addr,
                        &addrLen);
    if (nRet > 0)
    {
        // 查找/分配对应 UDP tunnel
        auto pt = getTunnel(curTime, pse, &addr);
        if (pt)
        {
            if (Endpoint::appendToSendList(pt->north, mpDynamicBuffer->cut(nRet)))
            {
                // 设置 write 标志
                epollResetEndpointMode(mForwardEpollfd, pt->north, true, true, false);
            }

            mTimeoutTimer.refresh(curTime, &pt->timerEntity);

            // 继续接收
            return true;
        }
        else
        {
            spdlog::trace("[UdpForwardService::southRead] tunnel closed");
        }
    }
    else if (nRet < 0)
    {
        if (errno == EAGAIN)
        {
            // 此次数据接收已完毕
        }
        else
        {
            spdlog::critical("[UdpForwardService::southRead] service soc[{}] fail: {}:[]",
                             pse->soc, errno, strerror(errno));
            pse->valid = false;
        }
    }
    else
    {
        spdlog::trace("[UdpForwardService::southRead] skip empty udp packet.");
    }

    return false;
}

void UdpForwardService::southWrite(time_t curTime, Endpoint_t *pse)
{
    if (!pse->valid)
    {
        spdlog::critical("[UdpForwardService::southRead] service soc[{}] not valid");
        if (pse->sendListHead)
        {
            // clean buffer list
            auto p = (DynamicBuffer::BufBlk_t *)pse->sendListHead;
            while (p)
            {
                auto next = p->next;
                mpDynamicBuffer->release(p);
                p = next;
            }
            pse->sendListHead = pse->sendListTail = nullptr;
            pse->totalBufSize = 0;
        }
        // stop write
        epollResetEndpointMode(mServiceEpollfd, pse, true, false, false);
        return;
    }

    auto p = (DynamicBuffer::BufBlk_t *)pse->sendListHead;
    if (!p)
    {
        // stop write
        epollResetEndpointMode(mServiceEpollfd, pse, true, false, false);
        return;
    }

    while (p)
    {
        auto it = mAddr2Tunnel.find(p->destAddr);
        if (it == mAddr2Tunnel.end())
        {
            // 已被移除 tunnel 的剩余数据
            spdlog::debug("[UdpForwardService::southWrite] drop closed tunnel[{}] pkt",
                          Utils::dumpSockAddr(p->destAddr));
            for (auto &it : mAddr2Tunnel)
            {
                spdlog::debug("[UdpForwardService::southWrite] addr: {}, tunnel: {}",
                              Utils::dumpSockAddr(it.first), Utils::dumpTunnel(it.second));
            }
        }
        else
        {
            auto nRet = sendto(pse->soc,
                               p->buffer,
                               p->dataSize,
                               0,
                               (sockaddr *)&p->destAddr,
                               sizeof(p->destAddr));
            if (nRet > 0)
            {
                assert(nRet == p->dataSize);
                mTimeoutTimer.refresh(curTime, &it->second->timerEntity);
            }
            else if (nRet < 0)
            {
                if (errno == EAGAIN)
                {
                    // 此次数据接收已完毕
                    break;
                }
                else
                {
                    spdlog::debug("[UdpForwardService::southWrite] soc[{}] send fail: {}:[]",
                                  Utils::dumpSockAddr(p->destAddr), errno, strerror(errno));

                    // close client tunnel
                    addToCloseList(it->second);
                }
            }
            else
            {
                // send ZERO data, drop pkt
                spdlog::warn("[UdpForwardService::southWrite] send ZERO data, drop pkt");
            }
        }

        // release sent buffer
        auto next = p->next;
        pse->totalBufSize -= p->dataSize;
        mpDynamicBuffer->release(p);
        p = next;
    }

    if (p)
    {
        // 还有数据包未发送
        pse->sendListHead = p;
        assert(pse->totalBufSize > 0);
    }
    else
    {
        //已无数据包需要发送
        pse->sendListHead = pse->sendListTail = nullptr;
        assert(pse->totalBufSize == 0);
    }
}

void UdpForwardService::northRead(time_t curTime, Endpoint_t *pe)
{
    if (!pe->valid)
    {
        spdlog::debug("[UdpForwardService::northRead] skip invalid tunnel[{}]");
        return;
    }

    while (true)
    {
        // 按最大 UDP 数据包预申请内存
        auto buffer = mpDynamicBuffer->reserve(PREALLOC_RECV_BUFFER_SIZE);
        if (buffer == nullptr)
        {
            // out of buffer
            spdlog::trace("[UdpForwardService::northRead] out of buffer");
            return;
        }

        if (!northRead(curTime, pe, buffer))
        {
            // 此次接收操作已完成
            break;
        }
    }
}

bool UdpForwardService::northRead(time_t curTime, Endpoint_t *pe, char *buffer)
{
    auto pt = (Tunnel_t *)pe->container;

    sockaddr_in addr;
    socklen_t addrLen = sizeof(sockaddr_in);
    int nRet = recvfrom(pe->soc, buffer, PREALLOC_RECV_BUFFER_SIZE, 0, (sockaddr *)&addr, &addrLen);
    if (nRet > 0)
    {
        // 判断数据包来源是否合法
        if (Utils::compareAddr(&addr, &pe->conn.remoteAddr))
        {
            // drop unknown incoming packet
            spdlog::debug("[UdpForwardService::northRead] drop invalid addr[{}] pkt at tunnel[{}] for {}",
                          Utils::dumpSockAddr(addr),
                          pe->soc, Utils::dumpSockAddr(pe->conn.remoteAddr));
        }
        else
        {
            // 取南向地址
            auto it = mSoc2SouthRemoteAddr.find(pe->soc);
            if (it == mSoc2SouthRemoteAddr.end())
            {
                // drop unknown incoming packet
                assert(!"[UdpForwardService::northRead] south addr not exist");
            }

            auto pBlk = mpDynamicBuffer->cut(nRet);
            pBlk->destAddr = it->second;

            if (Endpoint::appendToSendList(pe->peer, pBlk))
            {
                epollResetEndpointMode(mServiceEpollfd, pe->peer, true, true, false);
            }

            mTimeoutTimer.refresh(curTime, &pt->timerEntity);

            return true;
        }
    }
    else if (nRet < 0)
    {
        if (errno == EAGAIN)
        {
            // 此次数据接收已完毕
        }
        else
        {
            spdlog::error("[UdpForwardService::northRead] tunnel[{}] fail: {}:{}",
                          pe->soc, errno, strerror(errno));
            pe->valid = false;

            // close client tunnel
            addToCloseList((Tunnel_t *)pe->container);
        }
    }
    else
    {
        spdlog::trace("[UdpForwardService::northRead] skip empty udp packet.");
    }

    return false;
}

void UdpForwardService::northWrite(time_t curTime, Endpoint_t *pe)
{
    if (!pe->valid)
    {
        spdlog::error("[UdpForwardService::northWrite] skip invalid tunnel[{}]");
        if (pe->sendListHead)
        {
            // clean buffer list
            auto p = (DynamicBuffer::BufBlk_t *)pe->sendListHead;
            while (p)
            {
                auto next = p->next;
                mpDynamicBuffer->release(p);
                p = next;
            }
            pe->sendListHead = pe->sendListTail = nullptr;
            pe->totalBufSize = 0;
        }
        return;
    }

    auto p = (buffer::DynamicBuffer::BufBlk_t *)pe->sendListHead;
    if (!p)
    {
        // stop send
        epollResetEndpointMode(mForwardEpollfd, pe, true, false, false);
        return;
    }

    while (p)
    {
        int nRet = send(pe->soc, p->buffer, p->dataSize, 0);
        if (nRet > 0)
        {
            assert(nRet == p->dataSize);
            mTimeoutTimer.refresh(curTime, &((Tunnel_t *)pe->container)->timerEntity);
        }
        else if (nRet < 0)
        {
            if (errno == EAGAIN)
            {
                // 此次发送窗口已关闭
                break;
            }

            spdlog::debug("[UdpForwardService::northWrite] tunnel[{}] send fail: {} - {}",
                          pe->soc, errno, strerror(errno));
            pe->valid = false;

            // close client tunnel
            addToCloseList((Tunnel_t *)pe->container);

            break;
        }
        else
        {
            // send ZERO data, drop pkt
            spdlog::warn("[UdpForwardService::northWrite] send ZERO data, drop pkt");
        }

        pe->totalBufSize -= p->dataSize;
        auto next = p->next;
        mpDynamicBuffer->release(p);
        p = next;
    }

    if (p)
    {
        // 还有待发送数据
        pe->sendListHead = p;
        assert(pe->totalBufSize > 0);
    }
    else
    {
        // 数据发送完毕
        pe->sendListHead = pe->sendListTail = nullptr;
        assert(pe->totalBufSize == 0);
    }
}

void UdpForwardService::closeTunnels()
{
    if (!mCloseList.empty())
    {
        for (auto pt : mCloseList)
        {
            spdlog::debug("[UdpForwardService::closeTunnels] close tunnel[{}]",
                          pt->north->soc);

            // remove from maps
            int northSoc = pt->north->soc;
            auto &addr = mSoc2SouthRemoteAddr[northSoc];
            mAddr2Tunnel.erase(addr);
            mSoc2SouthRemoteAddr.erase(northSoc);
            mSoc2Tunnel.erase(northSoc);

            // remove from timer
            mTimeoutTimer.erase(&pt->timerEntity);

            // remove buffers
            releaseEndpointBuffer(pt->north);

            // close and release endpoint object
            ::close(northSoc);
            Endpoint::releaseEndpoint(pt->north);
            // release tunnel object
            Tunnel::releaseTunnel(pt);
        }

        mCloseList.clear();
    }
}

void UdpForwardService::releaseEndpointBuffer(Endpoint_t *pe)
{
    if (pe && pe->sendListHead)
    {
        auto pkt = (DynamicBuffer::BufBlk_t *)pe->sendListHead;
        while (pkt)
        {
            auto next = pkt->next;
            mpDynamicBuffer->release(pkt);
            pkt = next;
        }
        pe->sendListHead = pe->sendListTail = nullptr;
        pe->totalBufSize = 0;
    }
}

} // namespace link
} // namespace mapper
