#include "tcpForwardService.h"
#include <execinfo.h>
#include <time.h>
#include <sys/epoll.h>
#include <sstream>
#include <spdlog/spdlog.h>
#include "endpoint.h"
#include "tunnel.h"
#include "utils.h"

#include "schema.def"

using namespace std;
using namespace rapidjson;
using namespace mapper::buffer;
using namespace mapper::utils;

namespace mapper
{
namespace link
{

const uint32_t TcpForwardService::EPOLL_THREAD_RETRY_INTERVAL = 7;
const uint32_t TcpForwardService::EPOLL_MAX_EVENTS = 8;
const uint32_t TcpForwardService::INTERVAL_EPOLL_WAIT_TIME = 50;

/**
 * tunnel state machine:
 * 
 *             |
 *  CLOSED ----|--> INITIALIZED -----> CONNECT -----> ESTABLISHED
 *     A       |         |                |               |
 *     |       |         |                |               |
 *     |       |         *--------------->|<--------------*
 *     |       |                          |
 *     |       |                          V
 *     *-------|---------------------- BROKEN
 *             |
 */
const bool TcpForwardService::StateMaine[TUNNEL_STATE_COUNT][TUNNEL_STATE_COUNT] = {
    // CLOSED | INITIALIZED | CONNECT | ESTABLISHED | BROKEN
    {0, 1, 0, 0, 0}, // CLOSED
    {0, 0, 1, 0, 1}, // INITIALIZED
    {0, 0, 0, 1, 1}, // CONNECT
    {0, 0, 0, 0, 1}, // ESTABLISHED
    {1, 0, 0, 0, 0}, // BROKEN
};

TcpForwardService::TcpForwardService()
    : Service("TcpForwardService"),
      mEpollfd(0),
      mStopFlag(false),
      mpDynamicBuffer(nullptr)
{
}

TcpForwardService::~TcpForwardService()
{
    // TODO: 将 tunnel 纳入 mTunnelList 中管理
    // close existed tunnels
    for (auto pt : mTunnelList)
    {
        pt->north->valid = false;
        pt->south->valid = false;
        if (pt->stat == TUNSTAT_ESTABLISHED ||
            pt->stat == TUNSTAT_CONNECT)
        {
            pt->stat = TUNSTAT_BROKEN;
        }
        spdlog::trace("[TcpForwardService::closeTunnel] close existed tunnel[{}:{}]",
                      pt->south->soc, pt->north->soc);

        // close tunnel
        closeTunnel(pt);
    }
    mTunnelList.clear();
}

bool TcpForwardService::init(list<shared_ptr<Forward>> &forwardList,
                             Setting_t &setting)
{
    spdlog::debug("[TcpForwardService::init] init tcp forward service");

    // check existed thread
    if (mMainRoutineThread.joinable())
    {
        spdlog::warn("[TcpForwardService::init] forward service thread not stop.");
        return true;
    }
    mStopFlag = false;

    mSetting = setting;
    mForwardList.swap(forwardList);

    // create buffer
    spdlog::trace("[TcpForwardService::init] create buffer");
    mpDynamicBuffer = buffer::DynamicBuffer::allocDynamicBuffer(setting.bufferSize);
    if (!mpDynamicBuffer)
    {
        spdlog::error("[TcpForwardService::init] alloc buffer fail");
        return false;
    }

    // start thread
    spdlog::trace("[TcpForwardService::init] start thread");
    mMainRoutineThread = thread(&TcpForwardService::epollThread, this);

    return true;
}

void TcpForwardService::join()
{
    mMainRoutineThread.joinable() && (mMainRoutineThread.join(), true);
}

void TcpForwardService::stop()
{
    // set stop flag
    spdlog::trace("[TcpForwardService::stop] set stop flag");
    mStopFlag = true;
}

void TcpForwardService::close()
{
    spdlog::debug("[TcpForwardService::close] close tcp forward service");

    // stop thread
    spdlog::trace("[TcpForwardService::close] stop thread");
    mStopFlag = true;
    join();

    // release buffer
    spdlog::trace("[TcpForwardService::close] release buffer");
    mpDynamicBuffer && (DynamicBuffer::releaseDynamicBuffer(mpDynamicBuffer), mpDynamicBuffer = nullptr);
}

void TcpForwardService::postProcess(time_t curTime)
{
    if (!mPostProcessList.empty())
    {
        for (auto pt : mPostProcessList)
        {
            // 进行会话状态处理
            switch (pt->stat)
            {
            case TUNSTAT_CONNECT:
                spdlog::debug("[TcpForwardService::postProcess] remove connecting tunnel[{}:{}]",
                              pt->south->soc, pt->north->soc);
                setStatus(pt, TUNSTAT_BROKEN);
                switchTimer(mConnectTimer, mReleaseTimer, curTime, pt);
                break;
            case TUNSTAT_ESTABLISHED:
                spdlog::debug("[TcpForwardService::postProcess] remove established tunnel[{}:{}]",
                              pt->south->soc, pt->north->soc);
                setStatus(pt, TUNSTAT_BROKEN);
                // switch timeout container
                switchTimer(mSessionTimer, mReleaseTimer, curTime, pt);
                break;
            case TUNSTAT_INITIALIZED:
            case TUNSTAT_BROKEN:
                break;
            default:
                spdlog::critical("[TcpForwardService::postProcess] invalid tunnel status: {}",
                                 pt->stat);
                assert(false);
            }

            // close tunnel
            closeTunnel(pt);
        }

        mPostProcessList.clear();
    }
}

void TcpForwardService::scanTimeout(time_t curTime)
{
    // check connecting/established tunnel timeout
    list<TimerList::Entity_t *> timeoutList;
    auto f = [&](TimerList &timer, time_t timeoutTime) {
        timeoutList.clear();
        timer.getTimeoutList(timeoutTime, timeoutList);
        for (auto entity : timeoutList)
        {
            auto pt = (Tunnel_t *)entity->container;
            spdlog::debug("[TcpForwardService::scanTimeout] tunnel[{}:{}] timeout",
                          pt->south->soc, pt->north->soc);
            addToCloseList(pt);
        }
    };
    f(mConnectTimer, curTime - mSetting.connectTimeout);
    f(mSessionTimer, curTime - mSetting.sessionTimeout);

    // check broken tunnel timeout
    timeoutList.clear();
    mReleaseTimer.getTimeoutList(curTime - mSetting.releaseTimeout, timeoutList);
    for (auto entity : timeoutList)
    {
        auto pt = (Tunnel_t *)entity->container;
        spdlog::debug("[TcpForwardService::scanTimeout] broken tunnel[{}:{}] timeout",
                      pt->south->soc, pt->north->soc);
        setStatus(pt, TUNSTAT_CLOSED);
        closeTunnel(pt);
    }
}

void TcpForwardService::epollThread()
{
    spdlog::debug("[TcpForwardService::epollThread] tcp forward service thread start");

    while (!mStopFlag)
    {
        // init env
        spdlog::debug("[TcpForwardService::epollThread] init env");
        if (!initEnv())
        {
            spdlog::error("[TcpForwardService::epollThread] init fail. wait {} seconds",
                          EPOLL_THREAD_RETRY_INTERVAL);
            closeEnv();
            this_thread::sleep_for(chrono::seconds(EPOLL_THREAD_RETRY_INTERVAL));
            continue;
        }

        // main routine
        try
        {
            while (!mStopFlag)
            {
                if (!doEpoll(mEpollfd))
                {
                    spdlog::error("[TcpForwardService::epollThread] do epoll fail.");
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

            spdlog::error("[TcpForwardService::epollThread] catch an exception. {}", e.what());
            if (strings == nullptr)
            {
                spdlog::error("[TcpForwardService::epollThread] backtrace_symbols fail.");
            }
            else
            {
                for (int i = 0; i < addrNum; i++)
                    spdlog::error("[TcpForwardService::epollThread] {}", strings[i]);
                free(strings);
            }
        }

        // close env
        closeEnv();

        if (!mStopFlag)
        {
            spdlog::debug("[TcpForwardService::epollThread] sleep {} secnds and try again", EPOLL_THREAD_RETRY_INTERVAL);
            this_thread::sleep_for(chrono::seconds(EPOLL_THREAD_RETRY_INTERVAL));
        }
    }

    spdlog::debug("[TcpForwardService::epollThread] tcp forward service thread stop");
}

bool TcpForwardService::initEnv()
{
    // init epoll fd
    spdlog::trace("[TcpForwardService::initEnv] init epoll fd");
    if ((mEpollfd = epoll_create1(0)) < 0)
    {
        spdlog::error("[TcpForwardService::initEnv] Failed to create epolls. {} - {}",
                      errno, strerror(errno));
        return false;
    }

    // init tcp forward services
    spdlog::trace("[TcpForwardService::initEnv] init tcp forward services");
    for (auto &forward : mForwardList)
    {
        // get service address of specified interface and port
        spdlog::trace("[TcpForwardService::initEnv] get service address of specified interface and port");
        sockaddr_in sai;
        if (!Utils::getIntfAddr(forward->interface.c_str(), sai))
        {
            spdlog::error("[TcpForwardService::initEnv] get address of interface[{}] fail.", forward->interface);
            return false;
        }
        sai.sin_port = htons(atoi(forward->service.c_str()));

        // service 是否已经存在
        Endpoint_t *pse;
        auto it = mAddr2ServiceEndpoint.find(sai);
        if (it == mAddr2ServiceEndpoint.end())
        {
            // 新服务

            // create service endpoint
            spdlog::trace("[TcpForwardService::initEnv] create service endpoint");
            pse = Endpoint::getEndpoint(PROTOCOL_TCP, TO_SOUTH, TYPE_SERVICE);
            if (!pse)
            {
                spdlog::error("[TcpForwardService::initEnv] create service endpoint fail.");
                return false;
            }

            // create service soc
            spdlog::trace("[TcpForwardService::initEnv] create service soc");
            pse->soc = Utils::createServiceSoc(PROTOCOL_TCP, &sai, sizeof(sockaddr_in));
            if (pse->soc > 0)
            {
                pse->conn.localAddr = sai;
                mAddr2ServiceEndpoint[sai] = pse;
            }
            else
            {
                Endpoint::releaseEndpoint(pse);
                spdlog::error("[TcpForwardService::initEnv] create service soc for {}:{} fail.",
                              forward->interface, forward->service);
                return false;
            }

            // add service endpoint into epoll driver
            spdlog::trace("[TcpForwardService::initEnv] add service endpoint into epoll driver");
            if (!epollAddEndpoint(mEpollfd, pse, true, false, false))
            {
                spdlog::error("[TcpForwardService::init] add service endpoint into epoll driver fail.");
                return false;
            }

            spdlog::trace("[TcpForwardService::initEnv] create tcp forward service: {}",
                          Utils::dumpSockAddr(pse->conn.localAddr));
        }
        else
        {
            pse = it->second;
        }
        if (mTargetManager.addTarget(pse->soc,
                                     forward->targetHost.c_str(),
                                     forward->targetService.c_str(),
                                     PROTOCOL_TCP))
        {
            spdlog::info("[TcpForwardService::initEnv] service[{}] add target: {} -> {}:{}",
                         pse->soc,
                         Utils::dumpSockAddr(pse->conn.localAddr),
                         forward->targetHost, forward->targetService);
        }
        else
        {
            spdlog::error("[TcpForwardService::initEnv] get target[{}:{}] into target manager fail.",
                          forward->interface, forward->service);
            return false;
        }
    }

    return true;
}

void TcpForwardService::closeEnv()
{
    // close tcp forward services
    if (!mAddr2ServiceEndpoint.empty())
    {
        spdlog::trace("[TcpForwardService::closeEnv] close tcp forward services");
        for (auto it : mAddr2ServiceEndpoint)
        {
            spdlog::trace("[TcpForwardService::closeEnv] close tcp forward service: {}",
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
    spdlog::trace("[TcpForwardService::closeEnv] close epoll fd");
    mEpollfd && (::close(mEpollfd), mEpollfd = 0);
}

bool TcpForwardService::doEpoll(int epollfd)
{
    static time_t lastScanTime = 0;

    time_t curTime;
    struct epoll_event ee[EPOLL_MAX_EVENTS];

    int nRet = epoll_wait(epollfd, ee, EPOLL_MAX_EVENTS, INTERVAL_EPOLL_WAIT_TIME);
    curTime = time(nullptr);
    if (nRet > 0)
    {
        for (int i = 0; i < nRet; ++i)
        {
            link::Endpoint_t *pe = (link::Endpoint_t *)ee[i].data.ptr;

            if (pe->type == TYPE_SERVICE)
            {
                if (ee[i].events & EPOLLIN)
                {
                    // accept client
                    acceptClient(curTime, pe);
                }
            }
            else
            {
                doTunnelSoc(curTime, pe, ee[i].events);
            }
        }
    }
    else if (nRet < 0)
    {
        if (errno != EAGAIN && errno != EINTR)
        {
            spdlog::error("[TcpForwardService::epollThread] epoll fail. {} - {}",
                          errno, strerror(errno));
            return false;
        }
    }

    // post process
    postProcess(curTime);

    // scan timeout
    if (lastScanTime < curTime)
    {
        scanTimeout(curTime);
        lastScanTime = curTime;
    }

    return true;
}

void TcpForwardService::doTunnelSoc(time_t curTime, Endpoint_t *pe, uint32_t events)
{
    auto pt = (Tunnel_t *)pe->container;

    if (!pe->valid)
    {
        spdlog::trace("[TcpForwardService::doTunnelSoc] skip invalid soc[{}]", pe->soc);
        addToCloseList(pt);
        return;
    }

    if (pe->direction == TO_NORTH)
    {
        // to north socket

        // Write
        if (events & EPOLLOUT)
        {
            // CONNECT 状态处理
            if ((pt->stat == TUNSTAT_CONNECT))
            {
                if (events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR))
                {
                    // 连接失败
                    spdlog::error("[TcpForwardService::doTunnelSoc] north soc[{}] connect fail", pe->soc);
                    addToCloseList(pt);
                }
                else
                {
                    // 北向连接成功建立，添加南向 soc 到 epoll 中，并将被向 soc 修改为 收 模式
                    epollResetEndpointMode(mEpollfd, pt->north, true, false, false);
                    epollResetEndpointMode(mEpollfd, pt->south, true, false, false);

                    setStatus(pt, TUNSTAT_ESTABLISHED);

                    spdlog::debug("[TcpForwardService::doTunnelSoc] tunnel[{},{}] established.",
                                  pt->south->soc, pt->north->soc);

                    // 切换定时器
                    switchTimer(mConnectTimer, mSessionTimer, curTime, pt);
                }

                return;
            }

            pe->sendListHead && (onWrite(curTime, pe), true);
        }

        // Read
        (events & EPOLLIN && !pe->peer->bufferFull) && (onRead(curTime, events, pe), true);
    }
    else
    {
        // to south socket

        assert(pe->direction == TO_SOUTH);

        // Write
        (events & EPOLLOUT) && (onWrite(curTime, pe), true);

        // Read
        (events & EPOLLIN) && (onRead(curTime, events, pe), true);
    }
}

void TcpForwardService::setStatus(Tunnel_t *pt, TunnelState_t stat)
{
    if (pt->stat == stat)
    {
        return;
    }

    if (!StateMaine[pt->stat][stat])
    {
        // for (int x = 0; x < TUNNEL_STATE_COUNT; ++x)
        // {
        //     for (int y = 0; y < TUNNEL_STATE_COUNT; ++y)
        //     {
        //         printf("\t%s", StateMaine[x][y] ? "true" : "false");
        //     }
        //     printf("\n");
        // }

        spdlog::critical("[TcpForwardService::setSTatus] tunnel[{}:{}] invalid status convert: {} --> {}.",
                         pt->south->soc, pt->north->soc, pt->stat, stat);
        assert(false);
    }

    spdlog::trace("[TcpForwardService::setSTatus] tunnel[{}:{}] stat: {} --> {}.",
                  pt->south->soc, pt->north->soc, pt->stat, stat);
    pt->stat = stat;
}

Tunnel_t *TcpForwardService::getTunnel()
{
    // alloc resources
    Tunnel_t *pt = Tunnel::getTunnel();
    if (pt == nullptr)
    {
        spdlog::error("[TcpForwardService::getTunnel] alloc tunnel fail");
        return nullptr;
    }
    Endpoint_t *north = Endpoint::getEndpoint(PROTOCOL_TCP, TO_NORTH, TYPE_NORMAL);
    if (north == nullptr)
    {
        spdlog::error("[TcpForwardService::getTunnel] alloc north endpoint fail");
        Tunnel::releaseTunnel(pt);
        return nullptr;
    }
    Endpoint_t *south = Endpoint::getEndpoint(PROTOCOL_TCP, TO_SOUTH, TYPE_NORMAL);
    if (south == nullptr)
    {
        spdlog::error("[TcpForwardService::getTunnel] alloc south endpoint fail");
        Endpoint::releaseEndpoint(south);
        Tunnel::releaseTunnel(pt);
        return nullptr;
    }

    // link resources
    pt->north = north;
    pt->south = south;
    pt->service = this;
    north->peer = south;
    north->service = this;
    north->container = pt;
    south->peer = north;
    south->service = this;
    south->container = pt;

    setStatus(pt, TUNSTAT_INITIALIZED);

    return pt;
}

void TcpForwardService::acceptClient(time_t curTime, Endpoint_t *pse)
{
    // alloc resources
    Tunnel_t *pt = getTunnel();
    if (pt == nullptr)
    {
        spdlog::error("[TcpForwardService::acceptClient] out of tunnel");

        // reject new clients by accept and close it
        int soc = accept(pse->soc, nullptr, nullptr);
        (soc > 0) && ::close(soc);

        return;
    }

    // set status
    setStatus(pt, TUNSTAT_CONNECT);

    // add into timeout timer
    addToTimer(mConnectTimer, curTime, pt);

    if (![&]() -> bool {
            // accept client
            pt->south->conn.remoteAddrLen = sizeof(pt->south->conn.remoteAddr);
            pt->south->soc = accept(pse->soc,
                                    (sockaddr *)&pt->south->conn.remoteAddr,
                                    &pt->south->conn.remoteAddrLen);
            if (pt->south->soc == -1)
            {
                if (errno == EAGAIN)
                {
                    // 此次接收窗口已关闭
                }
                else
                {
                    spdlog::error("[TcpForwardService::acceptClient] accept fail: {} - {}", errno, strerror(errno));
                }
                return false;
            }
            spdlog::debug("[TcpForwardService::acceptClient] accept client[{}]: {}",
                          pt->south->soc, Utils::dumpSockAddr(pt->south->conn.remoteAddr));

            // set client socket to non-block mode
            if (!Utils::setSocAttr(pt->south->soc, true, false))
            {
                spdlog::error("[TcpForwardService::acceptClient] set socket to non-blocking mode fail");
                return false;
            }

            // create north socket
            pt->north->soc = Utils::createSoc(PROTOCOL_TCP, true);
            if (pt->north->soc <= 0)
            {
                spdlog::error("[TcpForwardService::acceptClient] create north socket fail");
                return false;
            }

            // connect to target
            if (!connect(curTime, pse, pt)) // status has been converted to 'CONNECT' in this function
            {
                spdlog::error("[TcpForwardService::acceptClient] connect to target fail");
                return false;
            }

            // add north soc into epoll driver
            if (!epollAddEndpoint(mEpollfd, pt->south, false, false, false) ||
                !epollAddEndpoint(mEpollfd, pt->north, false, true, false))
            {
                spdlog::error("[TcpForwardService::acceptClient] add endpoints into epoll driver fail");
                return false;
            }

            return true;
        }())
    {
        addToCloseList(pt);
        return;
    }

    spdlog::debug("[TcpForwardService::acceptClient] create tunnel[{}:{}]",
                  pt->south->soc, pt->north->soc);
}

bool TcpForwardService::connect(time_t curTime, Endpoint_t *pse, Tunnel_t *pt)
{
    // connect to host
    auto addr = mTargetManager.getAddr(pse->soc);
    if (!addr)
    {
        spdlog::error("[TcpForwardService::connect] get host addr fail.");
        return false;
    }
    else if (::connect(pt->north->soc, (sockaddr *)addr, sizeof(sockaddr_in)) < 0 &&
             errno != EALREADY &&
             errno != EINPROGRESS)
    {
        // report fail
        mTargetManager.failReport(curTime, addr);
        spdlog::error("[TcpForwardService::connect] connect fail. {} - {}",
                      errno, strerror(errno));
        return false;
    }

    pt->north->conn.remoteAddr = *addr;

    return true;
}

void TcpForwardService::onRead(time_t curTime, int events, Endpoint_t *pe)
{
    auto pt = (Tunnel_t *)pe->container;
    // 状态机
    switch (pt->stat)
    {
    case TUNSTAT_ESTABLISHED:
        break;
    case TUNSTAT_BROKEN:
        spdlog::debug("[TcpForwardService::onRead] soc[{}] stop recv on broken tunnel.", pe->soc);
        addToCloseList(pt);
        return;
    default:
        spdlog::critical("[TcpForwardService::onRead] soc[{}] with invalid tunnel status: {}",
                         pe->soc, pt->stat);
        assert(false);
    }

    if (!pe->peer->valid)
    {
        spdlog::trace("[TcpForwardService::onRead] skip invalid tunnel[{}:{}]",
                      pe->soc, pe->peer->soc);
        addToCloseList(pt);
        return;
    }
    if (events & EPOLLRDHUP)
    {
        // peer stop send
        spdlog::debug("[TcpForwardService::onRead] close soc[{}] due to peer stop send"), pe->soc;
        pe->valid = false;
        addToCloseList(pt);
        return;
    }

    bool isRead = false;
    while (true)
    {
        // is buffer full
        if (pe->peer->bufferFull)
        {
            // 缓冲区满
            break;
        }

        // 申请内存
        auto pBufBlk = mpDynamicBuffer->getCurBufBlk();
        if (pBufBlk == nullptr)
        {
            // out of buffer
            break;
        }

        int nRet = recv(pe->soc, pBufBlk->buffer, pBufBlk->getBufSize(), 0);
        if (nRet < 0)
        {
            if (errno == EAGAIN)
            {
                // 此次发送窗口已关闭
            }
            else
            {
                spdlog::debug("[TcpForwardService::onRead] soc[{}] recv fail: {} - [{}]",
                              pe->soc, errno, strerror(errno));
                pe->valid = false;
                addToCloseList(pt);
            }
            break;
        }
        else if (nRet == 0)
        {
            // closed by peer
            spdlog::debug("[TcpForwardService::onRead] soc[{}] closed by peer", pe->soc);
            pe->valid = false;
            addToCloseList(pt);
            break;
        }

        // cut buffer
        auto pBlk = mpDynamicBuffer->cut(nRet);
        // attach to peer's send list
        if (Endpoint::appendToSendList(pe->peer, pBlk))
        {
            epollResetEndpointMode(mEpollfd, pe->peer, true, true, false);
        }

        isRead = true;
    }

    if (isRead)
    {
        // refresh timer
        refreshTimer(curTime, pt);
    }
}

void TcpForwardService::onWrite(time_t curTime, Endpoint_t *pe)
{
    if (!pe->valid)
    {
        releaseEndpointBuffer(pe);
        addToCloseList(pe);
        return;
    }

    // 状态机
    auto pt = (Tunnel_t *)pe->container;
    switch (pt->stat)
    {
    case TUNSTAT_ESTABLISHED:
    case TUNSTAT_BROKEN:
        break;
    default:
        spdlog::critical("[TcpForwardService::onWrite] soc[{}] with invalid tunnel status: {}",
                         pe->soc, pt->stat);
        assert(false);
    }

    bool pktReleased = false;
    auto pkt = (DynamicBuffer::BufBlk_t *)pe->sendListHead;
    while (pkt)
    {
        // send data
        assert(pkt->dataSize >= pkt->sent);
        int nRet = send(pe->soc, pkt->buffer + pkt->sent, pkt->dataSize - pkt->sent, 0);
        if (nRet < 0)
        {
            if (errno == EAGAIN)
            {
                // 此次发送窗口已关闭
            }
            else
            {
                spdlog::debug("[TcpForwardService::onWrite] soc[{}] send fail: {} - [{}]",
                              pe->soc, errno, strerror(errno));
                pe->valid = false;
                addToCloseList(pe);
            }
            break;
        }
        else
        {
            pkt->sent += nRet;
            pe->totalBufSize -= nRet;
            assert(pe->totalBufSize >= 0);

            if (pkt->dataSize == pkt->sent)
            {
                // 数据包发送完毕，可回收
                auto next = pkt->next;
                mpDynamicBuffer->release(pkt);
                pkt = next;
            }

            pktReleased = true;
        }
    }
    if (pkt == nullptr)
    {
        // 发送完毕
        pe->sendListHead = pe->sendListTail = nullptr;
        assert(pe->totalBufSize == 0);
        epollResetEndpointMode(mEpollfd, pe, pe->valid, false, false);
    }
    else
    {
        // 还有数据需要发送
        pe->sendListHead = pkt;
        assert(pe->totalBufSize > 0);
    }

    if (pktReleased)
    {
        // refresh timer
        refreshTimer(curTime, pt);

        // 是否有缓冲区对象被释放，已有能力接收从南向来的数据
        if (pt->stat == TUNSTAT_ESTABLISHED && // 只在链路建立的状态下接收来自对端的数据
            pe->valid &&                       // 此节点有能力发送
            pe->bufferFull &&                  // 此节点当前缓冲区满
            pe->peer->valid)                   // 对端有能力接收
        {
            pe->bufferFull = false;
            epollResetEndpointMode(mEpollfd, pe->peer, true, pe->peer->sendListHead, false);
        }
    }
}

void TcpForwardService::closeTunnel(Tunnel_t *pt)
{
    switch (pt->stat)
    {
    case TUNSTAT_BROKEN:
    {
        if ((pt->north->sendListHead && pt->north->valid) ||
            (pt->south->sendListHead && pt->south->valid))
        {
            Endpoint_t *pe = (pt->north->sendListHead && pt->north->valid)
                                 ? pt->north
                                 : pt->south;

            // send last data
            epollResetEndpointMode(mEpollfd, pe, false, true, false);
        }
        else
        {
            setStatus(pt, TUNSTAT_CLOSED);
            closeTunnel(pt);
        }
    }
    break;
    case TUNSTAT_CLOSED:
        // release tunnel
        spdlog::debug("[TcpForwardService::closeTunnel] close tunnel[{}:{}]",
                      pt->south->soc, pt->north->soc);

        // remove from timer
        removeFromTimer(mReleaseTimer, pt);

        // release endpoint buffer
        releaseEndpointBuffer(pt->north);
        releaseEndpointBuffer(pt->south);

        // remove endpoints from epoll
        epollRemoveTunnel(mEpollfd, pt);

        // close socket
        pt->north->soc && (::close(pt->north->soc), pt->north->soc = 0);
        pt->south->soc && (::close(pt->south->soc), pt->south->soc = 0);

        // release objects
        Endpoint::releaseEndpoint(pt->north);
        Endpoint::releaseEndpoint(pt->south);
        Tunnel::releaseTunnel(pt);

        break;
    case TUNSTAT_INITIALIZED:
        // release tunnel
        spdlog::debug("[TcpForwardService::closeTunnel] close tunnel[{}:{}]",
                      pt->south->soc, pt->north->soc);

        // remove from timer
        removeFromTimer(mConnectTimer, pt);

        // remove endpoints from epoll
        epollRemoveTunnel(mEpollfd, pt);

        // close socket
        pt->north->soc && (::close(pt->north->soc), pt->north->soc = 0);
        pt->south->soc && (::close(pt->south->soc), pt->south->soc = 0);

        // release objects
        Endpoint::releaseEndpoint(pt->north);
        Endpoint::releaseEndpoint(pt->south);
        Tunnel::releaseTunnel(pt);
        break;
    default:
        spdlog::critical("[TcpForwardService::closeTunnel] invalid tunnel status: {}", pt->stat);
        assert(false);
        break;
    }
}

void TcpForwardService::refreshTimer(time_t curTime, Tunnel_t *pt)
{
    switch (pt->stat)
    {
    case TUNSTAT_ESTABLISHED:
        refreshTimer(mSessionTimer, curTime, pt);
        break;
    case TUNSTAT_BROKEN:
        refreshTimer(mReleaseTimer, curTime, pt);
        return;
    default:
        spdlog::error("[TcpForwardService::refreshTimer] tunnel[{}:{}] with invalid tunnel status: {}",
                      pt->south->soc, pt->north->soc, pt->stat);
        assert(false);
    }
}

void TcpForwardService::releaseEndpointBuffer(Endpoint_t *pe)
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
