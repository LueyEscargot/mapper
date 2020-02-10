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

#define ENABLE_DETAIL_LOGS
#undef ENABLE_DETAIL_LOGS

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
      mStopFlag(false),
      mUp(0),
      mDown(0),
      mTotalUp(0),
      mTotalDown(0)
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
    if (mNorthThread.joinable())
    {
        spdlog::warn("[UdpForwardService::init] forward service thread not stop.");
        return true;
    }
    mStopFlag = false;

    mSetting = setting;
    mForwardList.swap(forwardList);

    // create buffer
    spdlog::trace("[UdpForwardService::init] create buffer");
    mpToNorthDynamicBuffer = buffer::DynamicBuffer::allocDynamicBuffer(setting.bufferSize);
    // mpToSouthDynamicBuffer = buffer::DynamicBuffer::allocDynamicBuffer(setting.bufferSize);
    mpToSouthDynamicBuffer = mpToNorthDynamicBuffer;
    if (!mpToNorthDynamicBuffer || !mpToSouthDynamicBuffer)
    {
        spdlog::error("[UdpForwardService::init] alloc buffer fail");
        return false;
    }

    // start thread
    spdlog::trace("[UdpForwardService::init] start thread");
    mNorthThread = thread(&UdpForwardService::northThread, this);
    mSouthThread = thread(&UdpForwardService::southThread, this);

    return true;
}

void UdpForwardService::join()
{
    mNorthThread.joinable() && (mNorthThread.join(), true);
    mSouthThread.joinable() && (mSouthThread.join(), true);
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
    mpToNorthDynamicBuffer && (DynamicBuffer::releaseDynamicBuffer(mpToNorthDynamicBuffer), mpToNorthDynamicBuffer = nullptr);
    mpToSouthDynamicBuffer && (DynamicBuffer::releaseDynamicBuffer(mpToSouthDynamicBuffer), mpToSouthDynamicBuffer = nullptr);
}

string UdpForwardService::getStatistic(time_t curTime)
{
    static time_t lastTime = 0;
    time_t deltaTime = curTime - lastTime;
    lastTime = curTime;
    deltaTime = deltaTime ? deltaTime : 1;

    stringstream ss;

    ss << "u/d:" << Utils::toHumanStr(mUp / deltaTime) << "ps/" << Utils::toHumanStr(mDown / deltaTime)
       << "ps,tu/td:" << Utils::toHumanStr(mTotalUp) << "/" << Utils::toHumanStr(mTotalDown);

    return ss.str();
}

void UdpForwardService::resetStatistic()
{
    mUp = 0;
    mDown = 0;
}

void UdpForwardService::northThread()
{
    spdlog::debug("[UdpForwardService::northThread] udp forward service thread start");

    while (!mStopFlag)
    {
        // init env
        spdlog::debug("[UdpForwardService::northThread] init env");
        if (!initNorthEnv())
        {
            spdlog::error("[UdpForwardService::northThread] init fail. wait {} seconds",
                          EPOLL_THREAD_RETRY_INTERVAL);
            closeNorthEnv();
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

                // append to north packet list
                processToNorthPkts(curTime);

                if (!doNorthEpoll(curTime, mForwardEpollfd))
                {
                    spdlog::error("[UdpForwardService::northThread] do epoll fail.");
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

            spdlog::error("[UdpForwardService::northThread] catch an exception. {}", e.what());
            if (strings == nullptr)
            {
                spdlog::error("[UdpForwardService::northThread] backtrace_symbols fail.");
            }
            else
            {
                for (int i = 0; i < addrNum; i++)
                    spdlog::error("[UdpForwardService::northThread] {}", strings[i]);
                free(strings);
            }
        }

        // close env
        closeNorthEnv();

        if (!mStopFlag)
        {
            spdlog::debug("[UdpForwardService::northThread] sleep {} secnds and try again", EPOLL_THREAD_RETRY_INTERVAL);
            this_thread::sleep_for(chrono::seconds(EPOLL_THREAD_RETRY_INTERVAL));
        }
    }

    spdlog::debug("[UdpForwardService::northThread] udp forward service thread stop");
}

void UdpForwardService::southThread()
{
    spdlog::debug("[UdpForwardService::southThread] udp forward service thread start");

    while (!mStopFlag)
    {
        // init env
        spdlog::debug("[UdpForwardService::southThread] init env");
        if (!initSouthEnv())
        {
            spdlog::error("[UdpForwardService::southThread] init fail. wait {} seconds",
                          EPOLL_THREAD_RETRY_INTERVAL);
            closeSouthEnv();
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

                // append to south packet list
                processToSouthPkts(curTime);

                if (!doSouthEpoll(curTime, mServiceEpollfd))
                {
                    spdlog::error("[UdpForwardService::southThread] do epoll fail.");
                    break;
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

            spdlog::error("[UdpForwardService::southThread] catch an exception. {}", e.what());
            if (strings == nullptr)
            {
                spdlog::error("[UdpForwardService::southThread] backtrace_symbols fail.");
            }
            else
            {
                for (int i = 0; i < addrNum; i++)
                    spdlog::error("[UdpForwardService::southThread] {}", strings[i]);
                free(strings);
            }
        }

        // close env
        closeSouthEnv();

        if (!mStopFlag)
        {
            spdlog::debug("[UdpForwardService::southThread] sleep {} secnds and try again", EPOLL_THREAD_RETRY_INTERVAL);
            this_thread::sleep_for(chrono::seconds(EPOLL_THREAD_RETRY_INTERVAL));
        }
    }

    spdlog::debug("[UdpForwardService::southThread] udp forward service thread stop");
}

bool UdpForwardService::initNorthEnv()
{
    // init forward epoll fd
    spdlog::trace("[UdpForwardService::initNorthEnv] init forward epoll fd");
    if ((mForwardEpollfd = epoll_create1(EPOLL_CLOEXEC)) < 0)
    {
        spdlog::error("[UdpForwardService::initNorthEnv] Failed to create forward epoll fd. {} - {}",
                      errno, strerror(errno));
        return false;
    }

    return true;
}

bool UdpForwardService::initSouthEnv()
{
    // init service epoll fd
    spdlog::trace("[UdpForwardService::initSouthEnv] init service epoll fd");
    if ((mServiceEpollfd = epoll_create1(0)) < 0)
    {
        spdlog::error("[UdpForwardService::initSouthEnv] Failed to create service epoll fd. {} - {}",
                      errno, strerror(errno));
        return false;
    }

    // init udp forward services
    spdlog::trace("[UdpForwardService::initSouthEnv] init udp forward services");
    for (auto &forward : mForwardList)
    {
        // get service address of specified interface and port
        spdlog::trace("[UdpForwardService::initSouthEnv] get service address of specified interface and port");
        sockaddr_in sai;
        if (!Utils::getIntfAddr(forward->interface.c_str(), sai))
        {
            spdlog::error("[UdpForwardService::initSouthEnv] get address of interface[{}] fail.", forward->interface);
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
            spdlog::trace("[UdpForwardService::initSouthEnv] create service endpoint");
            pe = Endpoint::getEndpoint(PROTOCOL_UDP, TO_SOUTH, TYPE_SERVICE);
            if (!pe)
            {
                spdlog::error("[UdpForwardService::initSouthEnv] create service endpoint fail.");
                return false;
            }

            // create service soc
            spdlog::trace("[UdpForwardService::initSouthEnv] create service soc");
            pe->soc = Utils::createServiceSoc(PROTOCOL_UDP, &sai, sizeof(sockaddr_in));
            if (pe->soc > 0)
            {
                pe->conn.localAddr = sai;
                mAddr2ServiceEndpoint[sai] = pe;
            }
            else
            {
                Endpoint::releaseEndpoint(pe);
                spdlog::error("[UdpForwardService::initSouthEnv] create service soc for {}:{} fail.",
                              forward->interface, forward->service);
                return false;
            }

            // add service endpoint into epoll driver
            spdlog::trace("[UdpForwardService::initSouthEnv] add service endpoint into epoll driver");
            if (!epollAddEndpoint(mServiceEpollfd, pe, true, true, false))
            {
                spdlog::error("[UdpForwardService::init] add service endpoint into epoll driver fail.");
                return false;
            }

            spdlog::trace("[UdpForwardService::initSouthEnv] create udp forward service: {}",
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
            mAddr2ServiceEndpoint[pe->conn.localAddr] = pe;
#ifdef ENABLE_DETAIL_LOGS
            spdlog::debug("[UdpForwardService::initSouthEnv] set service endpoint by[{}]",
                          Utils::dumpSockAddr(pe->conn.localAddr));
#endif // ENABLE_DETAIL_LOGS

            spdlog::info("[UdpForwardService::initEnv] service[{}] add target: {} -> {}:{}",
                         pe->soc,
                         Utils::dumpSockAddr(pe->conn.localAddr),
                         forward->targetHost, forward->targetService);
        }
        else
        {
            spdlog::error("[UdpForwardService::initSouthEnv] add target[{}:{}] into target manager fail.",
                          forward->interface, forward->service);
            return false;
        }
    }

    return true;
}

void UdpForwardService::closeNorthEnv()
{
    // close epoll fd
    spdlog::trace("[UdpForwardService::closeNorthEnv] close forward epoll fd");
    mForwardEpollfd && (::close(mForwardEpollfd), mForwardEpollfd = 0);
}

void UdpForwardService::closeSouthEnv()
{
    // close udp forward services
    if (!mAddr2ServiceEndpoint.empty())
    {
        spdlog::trace("[UdpForwardService::closeSouthEnv] close udp forward services");
        for (auto it : mAddr2ServiceEndpoint)
        {
            spdlog::trace("[UdpForwardService::closeSouthEnv] close udp forward service: {}",
                          Utils::dumpSockAddr(it.second->conn.localAddr));

            if (it.second->soc)
            {
                // remove service socket from epoll
                epollRemoveEndpoint(mServiceEpollfd, it.second);

                // close socket
                ::close(it.second->soc);
                it.second->soc = 0;
            }
            // release endpoint_t object
            Endpoint::releaseEndpoint(it.second);
        }
        mAddr2ServiceEndpoint.clear();
    }

    // clean target manager
    mTargetManager.clear();

    // close epoll fd
    spdlog::trace("[UdpForwardService::closeSouthEnv] close service epoll fd");
    mServiceEpollfd && (::close(mServiceEpollfd), mServiceEpollfd = 0);
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

            if (ee[i].events & (EPOLLOUT | EPOLLIN))
            {
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
            else
            {
                spdlog::error("[UdpForwardService::doNorthEpoll] "
                              "endpoint[{}]: with error event: {}",
                              ee[i].events);
                addToCloseList(pe);
            }
        }
    }
    else if (nRet < 0)
    {
        if (errno != EAGAIN && errno != EINTR)
        {
            spdlog::error("[UdpForwardService::doNorthEpoll] epoll fail: {} - {}",
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
            link::Endpoint_t *pse = (link::Endpoint_t *)ee[i].data.ptr;
            assert(pse->type == TYPE_SERVICE && pse->direction == TO_SOUTH);

            // Write
            if (ee[i].events & EPOLLOUT)
            {
                southWrite(curTime, pse);
            }

            // Read
            if (ee[i].events & EPOLLIN)
            {
                southRead(curTime, pse);
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
            // save client's ip-port and target's ip-port
            north->conn.localAddr = *southRemoteAddr;
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
    socklen_t addrLen = sizeof(sockaddr_in);
    list<DynamicBuffer::BufBlk_t *> recvList;

    while (true)
    {

        // detect udp packet length
        int pktLen = recv(pse->soc, NULL, 0, MSG_PEEK | MSG_TRUNC);
        if (pktLen < 0)
        {
            if (errno == EAGAIN)
            {
                // 此次数据接收已完毕
                break;
            }
            else if (errno == EINTR)
            {
                // 此次数据接收被中断，继续尝试接收数据
                spdlog::debug("[UdpForwardService::southRead] broken by interrupt, try again.");
                continue;
            }
            else
            {
                spdlog::critical("[UdpForwardService::southRead] service soc[{}] fail: {}:[]",
                                 pse->soc, errno, strerror(errno));
                pse->valid = false;
                break;
            }
        }
        else if (pktLen == 0)
        {
            spdlog::trace("[UdpForwardService::southRead] skip empty udp packet.");
            continue;
        }

        // get buffer
        auto pBufBlk = mpToNorthDynamicBuffer->getBufBlk(pktLen);
        if (pBufBlk == nullptr)
        {
            // out of buffer
            spdlog::trace("[UdpForwardService::southRead] out of buffer, drop packet");
            recv(pse->soc, NULL, 0, 0);
            break;
        }

        // receive buffer
        recvfrom(pse->soc, pBufBlk->buffer, pktLen, 0, (sockaddr *)&pBufBlk->srcAddr, &addrLen);
        pBufBlk->dstAddr = pse->conn.localAddr;
        recvList.push_back(pBufBlk);

        // statistic
        mUp += pktLen;
        mTotalUp += pktLen;
    }

    // merge receive list
    {
        lock_guard<mutex> lg(mAccessMutex);
        mToNorthPktList.splice(mToNorthPktList.end(), recvList);
    }
}

void UdpForwardService::southWrite(time_t curTime, Endpoint_t *pse)
{
    auto pkt = (DynamicBuffer::BufBlk_t *)pse->sendListHead;
    if (!pkt)
    {
        // stop write
        epollResetEndpointMode(mServiceEpollfd, pse, true, false, false);
        return;
    }

    while (pkt)
    {
        auto nRet = sendto(pse->soc,
                           pkt->buffer,
                           pkt->dataSize,
                           0,
                           (sockaddr *)&pkt->dstAddr,
                           sizeof(sockaddr_in));
        if (nRet > 0)
        {
            assert(nRet == pkt->dataSize);
        }
        else if (nRet < 0)
        {
            if (errno == EAGAIN)
            {
                // 此次数据发送已完毕
                break;
            }
            else if (errno == EINTR)
            {
                // 此次数据发送被中断，继续尝试发送数据
                spdlog::debug("[UdpForwardService::southWrite] broken by interrupt, try again.");
                continue;
            }
            else
            {
                spdlog::debug("[UdpForwardService::southWrite] send to [{}] fail: {}:[]",
                              Utils::dumpSockAddr(pkt->dstAddr), errno, strerror(errno));
            }
        }
        else
        {
            // send ZERO data, drop pkt
            spdlog::warn("[UdpForwardService::southWrite] send ZERO data, drop pkt");
        }

        // release sent buffer
        auto next = pkt->next;
        pse->totalBufSize -= pkt->dataSize;

        mpToSouthDynamicBuffer->release(pkt);

        pkt = next;

        // statistic
        mDown += nRet;
        mTotalDown += nRet;
    }

    if (pkt)
    {
        // 还有数据包未发送
        pse->sendListHead = pkt;
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

    list<DynamicBuffer::BufBlk_t *> recvList;
    socklen_t addrLen = sizeof(sockaddr_in);

    while (true)
    {

        // detect udp packet length
        int pktLen = recv(pe->soc, NULL, 0, MSG_PEEK | MSG_TRUNC);

        if (pktLen < 0)
        {
            if (errno == EAGAIN)
            {
                // 此次数据接收已完毕
                break;
            }
            else if (errno == EINTR)
            {
                // 此次数据接收被中断，继续尝试接收数据
                spdlog::debug("[UdpForwardService::northRead] broken by interrupt, try again.");
                continue;
            }
            else
            {
                spdlog::critical("[UdpForwardService::northRead] soc[{}] fail: {}:[]",
                                 pe->soc, errno, strerror(errno));
                pe->valid = false;
                break;
            }
        }
        else if (pktLen == 0)
        {
            spdlog::trace("[UdpForwardService::northRead] skip empty udp packet.");
            continue;
        }

        // get buffer
        auto pBufBlk = mpToNorthDynamicBuffer->getBufBlk(pktLen);
        if (pBufBlk == nullptr)
        {
            // out of buffer
            spdlog::trace("[UdpForwardService::northRead] out of buffer, drop packet");
            recv(pe->soc, NULL, 0, 0);
            break;
        }

        auto pt = (Tunnel_t *)pe->container;

        int nRet = recvfrom(pe->soc, pBufBlk->buffer, pktLen, 0, (sockaddr *)&pBufBlk->srcAddr, &addrLen);

        // 判断数据包来源是否合法
        if (Utils::compareAddr(&pBufBlk->srcAddr, &pe->conn.remoteAddr))
        {
            // drop unknown incoming packet
            spdlog::debug("[UdpForwardService::northRead] drop invalid addr[{}] pkt at tunnel[{}] for {}. drop it",
                          Utils::dumpSockAddr(pBufBlk->srcAddr), pe->soc, Utils::dumpSockAddr(pe->conn.remoteAddr));
        }
        else
        {
            pBufBlk->srcAddr = pe->peer->conn.localAddr; // pe->conn.localAddr --> service's ip-port
            pBufBlk->dstAddr = pe->conn.localAddr;       // pe->conn.localAddr --> south(client)'s ip-port
            recvList.push_back(pBufBlk);

            mTimeoutTimer.refresh(curTime, &pt->timerEntity);
        }
    }

    // merge receive list
    {
        lock_guard<mutex> lg(mAccessMutex);
        mToSouthPktList.splice(mToSouthPktList.end(), recvList);
    }
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
                mpToNorthDynamicBuffer->release(p);
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
            else if (errno == EINTR)
            {
                // 此次数据发送被中断，继续尝试发送数据
                spdlog::debug("[UdpForwardService::northRead] broken by interrupt, try again.");
                continue;
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

        mpToNorthDynamicBuffer->release(p);

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

void UdpForwardService::processToNorthPkts(time_t curTime)
{
    list<DynamicBuffer::BufBlk_t *> pktList;
    {
        lock_guard<mutex> scopeLock(mAccessMutex);
        pktList.splice(pktList.end(), mToNorthPktList);
    }

    if (pktList.empty())
    {
        return;
    }
    for (auto pBufBlk : pktList)
    {
#ifdef ENABLE_DETAIL_LOGS
        spdlog::debug("[UdpForwardService::processToNorthPkts] search service endpoint by[{}]",
                      Utils::dumpSockAddr(pBufBlk->dstAddr));
#endif // ENABLE_DETAIL_LOGS

        // 查找对应 UDP tunnel
        auto pseIt = mAddr2ServiceEndpoint.find(pBufBlk->dstAddr);
        assert(pseIt != mAddr2ServiceEndpoint.end());

        // 查找/分配对应 UDP tunnel
        auto pt = getTunnel(curTime, pseIt->second, (sockaddr_in *)&pBufBlk->srcAddr);
        if (pt)
        {
            // append packets to send list
            pBufBlk->prev = nullptr;
            pBufBlk->next = nullptr;
            if (Endpoint::appendToSendList(pt->north, pBufBlk))
            {
                epollResetEndpointMode(mForwardEpollfd, pt->north, true, true, false);
            }
        }
        else
        {
            spdlog::trace("[UdpForwardService::processToNorthPkts] tunnel closed");
        }
    }
    pktList.clear();
}

void UdpForwardService::processToSouthPkts(time_t curTime)
{
    list<DynamicBuffer::BufBlk_t *> pktList;
    {
        lock_guard<mutex> scopeLock(mAccessMutex);
        pktList.splice(pktList.end(), mToSouthPktList);
    }

    if (pktList.empty())
    {
        return;
    }
    for (auto pBufBlk : pktList)
    {
#ifdef ENABLE_DETAIL_LOGS
        spdlog::debug("[UdpForwardService::processToSouthPkts] search service endpoint by[{}]",
                      Utils::dumpSockAddr(pBufBlk->dstAddr));
#endif // ENABLE_DETAIL_LOGS

        // 查找对应 Service Endpoint
        auto pseIt = mAddr2ServiceEndpoint.find(pBufBlk->srcAddr);
        assert(pseIt != mAddr2ServiceEndpoint.end());
        auto pse = pseIt->second;

        if (Endpoint::appendToSendList(pse, pBufBlk))
        {
            epollResetEndpointMode(mServiceEpollfd, pse, true, true, false);
        }
    }
    pktList.clear();
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
            mAddr2Tunnel.erase(pt->north->conn.localAddr); // pe->conn.localAddr --> south(client)'s ip-port
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
        assert(pe->direction == TO_NORTH);
        auto pkt = (DynamicBuffer::BufBlk_t *)pe->sendListHead;
        while (pkt)
        {

            auto next = pkt->next;
            mpToNorthDynamicBuffer->release(pkt);
            pkt = next;
        }
        pe->sendListHead = pe->sendListTail = nullptr;
        pe->totalBufSize = 0;
    }
}

} // namespace link
} // namespace mapper
