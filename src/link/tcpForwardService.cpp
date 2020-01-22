#include "tcpForwardService.h"
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
    : Service("TcpForwardService")
{
    mPostProcessList.clear();
}

TcpForwardService::~TcpForwardService()
{
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

bool TcpForwardService::init(int epollfd,
                             DynamicBuffer *pBuffer,
                             shared_ptr<Forward> forward,
                             Setting_t &setting)
{
    assert(Service::init(epollfd, pBuffer));

    mSetting = setting;

    mServiceEndpoint.init(PROTOCOL_TCP, TO_SOUTH, TYPE_SERVICE);
    mServiceEndpoint.service = this;

    // get local address of specified interface
    if (!Utils::getIntfAddr(forward->interface.c_str(), mServiceEndpoint.conn.localAddr))
    {
        spdlog::error("[TcpForwardService::init] get address of interface[{}] fail.", forward->interface);
        return false;
    }
    mServiceEndpoint.conn.localAddr.sin_port = htons(atoi(forward->service.c_str()));

    // create server socket
    mServiceEndpoint.conn.localAddrLen = sizeof(mServiceEndpoint.conn.localAddr);
    mServiceEndpoint.soc = Utils::createServiceSoc(PROTOCOL_TCP,
                                                   &mServiceEndpoint.conn.localAddr,
                                                   mServiceEndpoint.conn.localAddrLen);
    if (mServiceEndpoint.soc < 0)
    {
        spdlog::error("[TcpForwardService::init] create server socket fail.");
        return false;
    }

    // init target manager
    if (!mTargetManager.addTarget(time(nullptr),
                                  forward->targetHost.c_str(),
                                  forward->targetService.c_str(),
                                  PROTOCOL_TCP))
    {
        spdlog::error("[TcpForwardService::init] ginit target manager fail");
        close();
        return false;
    }

    // add service's endpoint into epoll driver
    if (!epollAddEndpoint(&mServiceEndpoint, true, false, false))
    {
        spdlog::error("[TcpForwardService::init] add service endpoint[{}] into epoll fail.",
                      forward->toStr());
        close();
        return false;
    }

    return true;
}

void TcpForwardService::close()
{
    // close service socket
    if (mServiceEndpoint.soc > 0)
    {
        ::close(mServiceEndpoint.soc);
        mServiceEndpoint.soc = 0;
    }
}

void TcpForwardService::onSoc(time_t curTime, uint32_t events, Endpoint_t *pe)
{
    if (pe->type == TYPE_SERVICE)
    {
        if (events & EPOLLIN)
        {
            // accept client
            acceptClient(curTime, pe);
        }
    }
    else
    {
        auto pt = (Tunnel_t *)pe->container;

        if (!pe->valid)
        {
            spdlog::trace("[TcpForwardService::onSoc] skip invalid soc[{}]", pe->soc);
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
                        spdlog::error("[TcpForwardService::onSoc] north soc[{}] connect fail", pe->soc);
                        addToCloseList(pt);
                    }
                    else
                    {
                        // 北向连接成功建立，添加南向 soc 到 epoll 中，并将被向 soc 修改为 收发 模式
                        if (epollResetEndpointMode(pt->north, true, true, true) &&
                            epollResetEndpointMode(pt->south, true, true, true))
                        {
                            setStatus(pt, TUNSTAT_ESTABLISHED);

                            spdlog::debug("[TcpForwardService::onSoc] tunnel[{},{}] established.",
                                          pt->south->soc, pt->north->soc);

                            // 切换定时器
                            switchTimer(mConnectTimer, mSessionTimer, curTime, pt);
                        }
                        else
                        {
                            spdlog::error("[TcpForwardService::onSoc] tunnel[{}-{}] reset epoll mode fail",
                                          pt->south->soc, pe->soc);
                            addToCloseList(pt);
                        }
                    }

                    return;
                }

                if (pe->sendListHead)
                {
                    onWrite(curTime, pe);
                }
            }

            // Read
            if (events & EPOLLIN && !pe->stopRecv)
            {
                onRead(curTime, events, pe);
                // 尝试发送数据
                if (pe->peer->sendListHead)
                {
                    onWrite(curTime, pe->peer);
                }
            }
        }
        else
        {
            // to south socket

            assert(pe->direction == TO_SOUTH);

            // Write
            if ((events & EPOLLOUT) && pe->sendListHead)
            {
                onWrite(curTime, pe);
            }

            // Read
            if (events & EPOLLIN && !pe->stopRecv)
            {
                onRead(curTime, events, pe);
                // 尝试发送数据
                if (pe->peer->sendListHead)
                {
                    onWrite(curTime, pe->peer);
                }
            }
        }
    }
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

void TcpForwardService::processBufferWaitingList(time_t curTime)
{
    if (!mpBuffer->empty() && mBufferWaitList.mpHead)
    {
        auto entry = mBufferWaitList.mpHead;
        while (entry)
        {
            auto pBufBlk = mpBuffer->getCurBufBlk();
            if (pBufBlk == nullptr)
            {
                // 已无空闲可用缓冲区
                break;
            }

            auto pe = (Endpoint_t *)entry->container;
            int nRet = recv(pe->soc, pBufBlk->buffer, pBufBlk->getBufSize(), 0);
            if (nRet < 0)
            {
                if (errno == EAGAIN) // 此端口的送窗口关闭 还是 有错误发生
                {
                    spdlog::debug("[TcpForwardService::processBufferWaitingList] soc[{}] EAGAIN", pe->soc);
                }
                else
                {
                    spdlog::debug("[TcpForwardService::processBufferWaitingList] soc[{}] recv fail: {} - [{}]",
                                  pe->soc, errno, strerror(errno));
                    pe->valid = false;
                    addToCloseList(pe);
                }
            }
            else if (nRet == 0)
            {
                // closed by peer
                spdlog::debug("[TcpForwardService::processBufferWaitingList] soc[{}] closed by peer", pe->soc);
                pe->valid = false;
                addToCloseList(pe);
            }
            else
            {
                // cut buffer
                auto pBlk = mpBuffer->cut(nRet);
                // attach to peer's send list
                Endpoint::appendToSendList(pe->peer, pBlk);

                // 重置停止接收标志
                pe->stopRecv = false;

                // 重置对应会话收发事件
                if (!epollResetEndpointMode(pe, true, true, true) ||
                    !epollResetEndpointMode(pe->peer, true, true, true))
                {
                    // closed by peer
                    spdlog::debug("[TcpForwardService::processBufferWaitingList] soc[{}] reset fail",
                                  pe->soc);
                    pe->valid = false;
                    addToCloseList(pe);
                }
            }

            // 将当前节点从等待队列中移除
            auto next = entry->next;
            mBufferWaitList.erase(entry);
            entry = next;
        }
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

void TcpForwardService::acceptClient(time_t curTime, Endpoint_t *pe)
{
    // alloc resources
    Tunnel_t *pt = getTunnel();
    if (pt == nullptr)
    {
        spdlog::error("[TcpForwardService::acceptClient] out of tunnel");

        // reject new clients by accept and close it
        int soc = accept(pe->soc, nullptr, nullptr);
        (soc > 0) && ::close(soc);

        return;
    }

    if (![&]() -> bool {
            // accept client
            pt->south->conn.remoteAddrLen = sizeof(pt->south->conn.remoteAddr);
            pt->south->soc = accept(pe->soc,
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
            if (!connect(curTime, pt)) // status has been converted to 'CONNECT' in this function
            {
                spdlog::error("[TcpForwardService::acceptClient] connect to target fail");
                return false;
            }

            // add north soc into epoll driver
            if (!epollAddEndpoint(pt->south, false, true, true) ||
                !epollAddEndpoint(pt->north, false, true, true))
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

    // add into timeout timer
    addToTimer(mConnectTimer, curTime, pt);

    spdlog::debug("[TcpForwardService::acceptClient] create tunnel[{}:{}]",
                  pt->south->soc, pt->north->soc);
}

bool TcpForwardService::connect(time_t curTime, Tunnel_t *pt)
{
    // check status
    setStatus(pt, TUNSTAT_CONNECT);

    // connect to host
    auto addrs = mTargetManager.getAddr(curTime);
    if (!addrs)
    {
        spdlog::error("[TcpForwardService::connect] get host addr fail.");
        return false;
    }
    else if (::connect(pt->north->soc, &addrs->addr, addrs->addrLen) < 0 &&
             errno != EALREADY &&
             errno != EINPROGRESS)
    {
        // report fail
        mTargetManager.failReport(curTime, &addrs->addr);
        spdlog::error("[TcpForwardService::connect] connect fail. {} - {}",
                      errno, strerror(errno));
        return false;
    }

    pt->north->conn.remoteAddr = *(sockaddr_in *)&addrs->addr;
    pt->north->conn.remoteAddrLen = addrs->addrLen;

    return true;
}

void TcpForwardService::onRead(time_t curTime, int events, Endpoint_t *pe)
{
    if (pe->bufWaitEntity.inList)
    {
        // 在等待缓存区队列中，此时不用处理
        return;
    }

    auto pt = (Tunnel_t *)pe->container;
    // 状态机
    switch (pt->stat)
    {
    case TUNSTAT_ESTABLISHED:
        break;
    case TUNSTAT_BROKEN:
        spdlog::debug("[TcpForwardService::onRead] soc[{}] stop recv - tunnel broken.", pe->soc);
        addToCloseList(pt);
        return;
    default:
        spdlog::critical("[TcpForwardService::onRead] soc[{}] with invalid tunnel status: {}",
                         pe->soc, pt->stat);
        assert(false);
    }

    if (!pe->valid || !pe->peer->valid)
    {
        spdlog::trace("[TcpForwardService::onRead] skip invalid tunnel[{}:{}]",
                      pe->soc, pe->peer->soc);
        return;
    }

    bool isRead = false;
    while (true)
    {
        // is buffer full
        if (pe->peer->bufferFull)
        {
            // 缓冲区满
            pe->stopRecv = true;
            break;
        }

        // 申请内存
        auto pBufBlk = mpBuffer->getCurBufBlk();
        if (pBufBlk == nullptr)
        {
            if (events & EPOLLRDHUP)
            {
                // peer stop send
                spdlog::debug("[TcpForwardService::onRead] close soc[{}] due to peer stop send");
                pe->valid = false;
                addToCloseList(pt);
            }
            else
            {
                // out of buffer
                // spdlog::trace("[TcpForwardService::onRead] soc[{}] out of buffer", pe->soc);
                pe->stopRecv = true;

                // append into buffer waiting list
                mBufferWaitList.push_back(&pe->bufWaitEntity);
            }
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
        auto pBlk = mpBuffer->cut(nRet);
        // attach to peer's send list
        Endpoint::appendToSendList(pe->peer, pBlk);

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
        auto pkt = (DynamicBuffer::BufBlk_t *)pe->sendListHead;
        while (pkt)
        {
            auto next = pkt->next;
            mpBuffer->release(pkt);
            pkt = next;
        }
        pe->sendListHead = pe->sendListTail = nullptr;
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
                mpBuffer->release(pkt);
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
            pe->peer->valid &&                 // 对端有能力接收
            pe->peer->stopRecv)                // 对端正处于停止接收状态
        {
            if (epollResetEndpointMode(pe->peer, true, true, true))
            {
                pe->bufferFull = false;
                pe->peer->stopRecv = false;
            }
            else
            {
                spdlog::error("[TcpForwardService::onWrite] force peer soc[{}] read fail",
                              pe->peer->soc);
                pe->peer->valid = false;
                addToCloseList(pt);
            }
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
            if (!epollResetEndpointMode(pe, false, true, true))
            {
                spdlog::error("[TcpForwardService::closeTunnel] reset sock[{}] in epoll fail. {} - {}",
                              pe->soc, errno, strerror(errno));
                pe->valid = false;
                setStatus(pt, TUNSTAT_CLOSED);
                closeTunnel(pt);
            }
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

        // remove from waiting buffer list
        if (pt->north->bufWaitEntity.inList)
        {
            spdlog::trace("[TcpForwardService::closeTunnel] remove north soc[{}]"
                          " from buffer waiting list",
                          pt->north->soc);
            mBufferWaitList.erase(&pt->north->bufWaitEntity);
        }
        if (pt->south->bufWaitEntity.inList)
        {
            spdlog::trace("[TcpForwardService::closeTunnel] remove south soc[{}]"
                          " from buffer waiting list",
                          pt->south->soc);
            mBufferWaitList.erase(&pt->south->bufWaitEntity);
        }

        // remove from timer
        removeFromTimer(mReleaseTimer, pt);

        // release buffer
        if (pt->north->sendListHead)
        {
            auto pBufBlk = (DynamicBuffer::BufBlk_t *)pt->north->sendListHead;
            while (pBufBlk)
            {
                mpBuffer->release(pBufBlk);
                pBufBlk = pBufBlk->next;
            }
            pt->north->sendListHead = nullptr;
        }
        if (pt->south->sendListHead)
        {
            auto pBufBlk = (DynamicBuffer::BufBlk_t *)pt->south->sendListHead;
            while (pBufBlk)
            {
                mpBuffer->release(pBufBlk);
                pBufBlk = pBufBlk->next;
            }
            pt->south->sendListHead = nullptr;
        }

        // remove endpoints from epoll
        epollRemoveTunnel(pt);

        // close socket
        if (pt->north->soc)
        {
            ::close(pt->north->soc);
            pt->north->soc = 0;
        }
        if (pt->south->soc)
        {
            ::close(pt->south->soc);
            pt->south->soc = 0;
        }

        // release objects
        Endpoint::releaseEndpoint(pt->north);
        Endpoint::releaseEndpoint(pt->south);
        Tunnel::releaseTunnel(pt);

        break;
    case TUNSTAT_INITIALIZED:
        // release tunnel
        epollRemoveTunnel(pt);

        // release objects
        Endpoint::releaseEndpoint(pt->north);
        Endpoint::releaseEndpoint(pt->south);
        Tunnel::releaseTunnel(pt);

        spdlog::debug("[TcpForwardService::closeTunnel] close tunnel[{}:{}]",
                      pt->south->soc, pt->north->soc);
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

} // namespace link
} // namespace mapper
