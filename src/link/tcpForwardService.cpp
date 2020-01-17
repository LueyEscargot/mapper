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
    for (auto pt : mTunnelList)
    {
        if (pt->north->soc)
        {
            ::close(pt->north->soc);
            pt->north->valid = false;
        }
        if (pt->south->soc)
        {
            ::close(pt->south->soc);
            pt->south->valid = false;
        }
        if (pt->stat == TunnelState_t::ESTABLISHED ||
            pt->stat == TunnelState_t::CONNECT)
        {
            pt->stat = TunnelState_t::BROKEN;
        }

        addToCloseList(pt);
    }

    // TODO: close existed tunnels
}

bool TcpForwardService::init(int epollfd,
                             DynamicBuffer *pBuffer,
                             shared_ptr<Forward> forward,
                             Setting_t &setting)
{
    assert(Service::init(epollfd, pBuffer));

    mSetting = setting;

    mServiceEndpoint.init(Protocol_t::TCP,
                          Direction_t::DIR_SOUTH,
                          Type_t::SERVICE);
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
    mServiceEndpoint.soc = Utils::createServiceSoc(Protocol_t::TCP,
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
                                  Protocol_t::TCP))
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

void TcpForwardService::setTimeout(TunnelState_t stat, const uint32_t interval)
{
    // switch (stat)
    // {
    // case TunnelState_t::CONNECT:
    //     mTimeoutInterval_Conn = interval;
    //     break;
    // case TunnelState_t::ESTABLISHED:
    //     mTimeoutInterval_Estb = interval;
    //     break;
    // case TunnelState_t::BROKEN:
    //     mTimeoutInterval_Brok = interval;
    //     break;
    // default:
    //     assert(!"invalid stat");
    //     break;
    // }
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
    if (pe->type == Type_t::SERVICE)
    {
        if (events & EPOLLIN)
        {
            // accept client
            acceptClient(curTime, pe);
        }
    }
    else
    {
        assert(pe->type == Type_t::NORMAL);
        auto pt = (UdpTunnel_t *)pe->container;

        if (!pe->valid)
        {
            spdlog::debug("[TcpForwardService::onSoc] skip invalid soc[{}]", pe->soc);
            addToCloseList(pt);
            return;
        }

        if (pe->direction == Direction_t::DIR_NORTH)
        {
            // to north socket

            // Write
            if (events & EPOLLOUT)
            {
                // CONNECT 状态处理
                if ((pt->stat == TunnelState_t::CONNECT))
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
                            setStatus(pt, TunnelState_t::ESTABLISHED);

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

            assert(pe->direction == Direction_t::DIR_SOUTH);

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
    // 处理缓冲区等待队列
    processBufferWaitingList();

    if (!mPostProcessList.empty())
    {
        for (auto pt : mPostProcessList)
        {
            // 进行会话状态处理
            switch (pt->stat)
            {
            case TunnelState_t::CONNECT:
                spdlog::debug("[TcpForwardService::postProcess] remove connecting tunnel[{}:{}]",
                              pt->south->soc, pt->north->soc);
                setStatus(pt, TunnelState_t::BROKEN);
                switchTimer(mConnectTimer, mReleaseTimer, curTime, pt);
                break;
            case TunnelState_t::ESTABLISHED:
                spdlog::debug("[TcpForwardService::postProcess] remove established tunnel[{}:{}]",
                              pt->south->soc, pt->north->soc);
                setStatus(pt, TunnelState_t::BROKEN);
                // switch timeout container
                switchTimer(mSessionTimer, mReleaseTimer, curTime, pt);
                break;
            case TunnelState_t::INITIALIZED:
            case TunnelState_t::BROKEN:
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
    // processBufferWaitingList();

    // check connecting/established tunnel timeout
    list<TimerList::Entity_t *> timeoutList;
    auto f = [&](TimerList &timer, time_t timeoutTime) {
        timeoutList.clear();
        timer.getTimeoutList(timeoutTime, timeoutList);
        for (auto entity : timeoutList)
        {
            auto pt = (UdpTunnel_t *)entity->container;
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
        auto pt = (UdpTunnel_t *)entity->container;
        spdlog::debug("[TcpForwardService::scanTimeout] broken tunnel[{}:{}] timeout",
                      pt->south->soc, pt->north->soc);
        setStatus(pt, TunnelState_t::CLOSED);
        closeTunnel(pt);
    }
}

void TcpForwardService::setStatus(UdpTunnel_t *pt, TunnelState_t stat)
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

UdpTunnel_t *TcpForwardService::getTunnel()
{
    // alloc resources
    UdpTunnel_t *pt = Tunnel::getTunnel();
    if (pt == nullptr)
    {
        spdlog::error("[TcpForwardService::getTunnel] alloc tunnel fail");
        return nullptr;
    }
    Endpoint_t *north = Endpoint::getEndpoint(Protocol_t::TCP,
                                              Direction_t::DIR_NORTH,
                                              Type_t::NORMAL);
    if (north == nullptr)
    {
        spdlog::error("[TcpForwardService::getTunnel] alloc north endpoint fail");
        Tunnel::releaseTunnel(pt);
        return nullptr;
    }
    Endpoint_t *south = Endpoint::getEndpoint(Protocol_t::TCP,
                                              Direction_t::DIR_SOUTH,
                                              Type_t::NORMAL);
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

    setStatus(pt, TunnelState_t::INITIALIZED);

    return pt;
}

void TcpForwardService::acceptClient(time_t curTime, Endpoint_t *pe)
{
    // alloc resources
    UdpTunnel_t *pt = getTunnel();
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
            pt->north->soc = Utils::createSoc(Protocol_t::TCP, true);
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

bool TcpForwardService::connect(time_t curTime, UdpTunnel_t *pt)
{
    // check status
    setStatus(pt, TunnelState_t::CONNECT);

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

bool TcpForwardService::epollAddEndpoint(Endpoint_t *pe, bool read, bool write, bool edgeTriger)
{
    // spdlog::debug("[TcpForwardService::epollAddEndpoint] endpoint[{}], read[{}], write[{}]",
    //               Utils::dumpEndpoint(pe), read, write);

    struct epoll_event event;
    event.data.ptr = pe;
    event.events = EPOLLRDHUP |                // for peer close
                   (read ? EPOLLIN : 0) |      // enable read
                   (write ? EPOLLOUT : 0) |    // enable write
                   (edgeTriger ? EPOLLET : 0); // use edge triger or level triger
    if (epoll_ctl(mEpollfd, EPOLL_CTL_ADD, pe->soc, &event))
    {
        spdlog::error("[TcpForwardService::epollAddEndpoint] events[{}]-soc[{}] join fail. Error {}: {}",
                      event.events, pe->soc, errno, strerror(errno));
        return false;
    }

    // spdlog::debug("[TcpForwardService::epollAddEndpoint] endpoint[{}], event.events[0x{:X}]",
    //               Utils::dumpEndpoint(pe), event.events);

    return true;
}

bool TcpForwardService::epollResetEndpointMode(Endpoint_t *pe, bool read, bool write, bool edgeTriger)
{
    // spdlog::debug("[TcpForwardService::epollResetEndpointMode] endpoint[{}], read: {}, write: {}, edgeTriger: {}",
    //               Utils::dumpEndpoint(pe), read, write, edgeTriger);

    struct epoll_event event;
    event.data.ptr = pe;
    event.events = EPOLLRDHUP |                // for peer close
                   (read ? EPOLLIN : 0) |      // enable read
                   (write ? EPOLLOUT : 0) |    // enable write
                   (edgeTriger ? EPOLLET : 0); // use edge triger or level triger
    if (epoll_ctl(mEpollfd, EPOLL_CTL_MOD, pe->soc, &event))
    {
        spdlog::error("[TcpForwardService::epollResetEndpointMode] events[{}]-soc[{}] reset fail. Error {}: {}",
                      event.events, pe->soc, errno, strerror(errno));
        return false;
    }

    return true;
}

bool TcpForwardService::epollResetEndpointMode(UdpTunnel_t *pt, bool read, bool write, bool edgeTriger)
{
    return epollResetEndpointMode(pt->north, read, write, edgeTriger) &&
           epollResetEndpointMode(pt->south, read, write, edgeTriger);
}

void TcpForwardService::epollRemoveEndpoint(Endpoint_t *pe)
{
    // spdlog::trace("[TcpForwardService::epollRemoveEndpoint] remove endpoint[{}]",
    //               Utils::dumpEndpoint(pe));

    // remove from epoll driver
    if (epoll_ctl(mEpollfd, EPOLL_CTL_DEL, pe->soc, nullptr))
    {
        spdlog::error("[TcpForwardService::epollRemoveEndpoint] remove endpoint[{}] from epoll fail. {} - {}",
                      Utils::dumpEndpoint(pe), errno, strerror(errno));
    }
}

void TcpForwardService::epollRemoveTunnel(UdpTunnel_t *pt)
{
    epollRemoveEndpoint(pt->north);
    epollRemoveEndpoint(pt->south);
}

void TcpForwardService::onRead(time_t curTime, int events, Endpoint_t *pe)
{
    if (pe->bufWaitEntry.inList)
    {
        // 在等待缓存区队列中，此时不用处理
        return;
    }

    auto pt = (UdpTunnel_t *)pe->container;
    // 状态机
    switch (pt->stat)
    {
    case TunnelState_t::ESTABLISHED:
        break;
    case TunnelState_t::BROKEN:
        spdlog::debug("[TcpForwardService::onRead] soc[{}] stop recv - tunnel broken.", pe->soc);
        addToCloseList(pt);
        return;
    default:
        spdlog::error("[TcpForwardService::onRead] soc[{}] with invalid tunnel status: {}",
                      pe->soc, pt->stat);
        assert(false);
    }

    if (!pe->valid || !pe->peer->valid)
    {
        spdlog::error("[TcpForwardService::onRead] skip recv when tunnel[{}:{}] invalid",
                      pe->soc, pe->peer->soc);
        addToCloseList(pt);
        return;
    }

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
                mBufferWaitList.push_back(curTime, &pe->bufWaitEntry);
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
        appendToSendList(pe->peer, pBlk);
    }

    return;
}

void TcpForwardService::onWrite(time_t curTime, Endpoint_t *pe)
{
    if (!pe->valid)
    {
        addToCloseList(pe);
        return;
    }

    // 状态机
    auto pt = (UdpTunnel_t *)pe->container;
    switch (pt->stat)
    {
    case TunnelState_t::ESTABLISHED:
    case TunnelState_t::BROKEN:
        break;
    default:
        spdlog::error("[TcpForwardService::onWrite] soc[{}] with invalid tunnel status: {}",
                      pe->soc, pt->stat);
        assert(false);
    }

    if (!pe->valid)
    {
        spdlog::debug("[TcpForwardService::onWrite] can't send on invalid soc[{}]", pe->soc);
        return;
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

    // 是否有缓冲区对象被释放，已有能力接收从南向来的数据
    if (pktReleased &&
        pt->stat == TunnelState_t::ESTABLISHED && // 只在链路建立的状态下接收来自对端的数据
        pe->valid &&                              // 此节点有能力发送
        pe->bufferFull &&                         // 此节点当前缓冲区满
        pe->peer->valid &&                        // 对端有能力接收
        pe->peer->stopRecv)                       // 对端正处于停止接收状态
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

void TcpForwardService::appendToSendList(Endpoint_t *pe, buffer::DynamicBuffer::BufBlk_t *pBlk)
{
    pBlk->next = nullptr;

    if (pe->sendListHead)
    {
        // 已有待发送数据包

        // auto head = (buffer::DynamicBuffer::BufBlk_t *)pe->sendListHead;
        auto tail = (buffer::DynamicBuffer::BufBlk_t *)pe->sendListTail;

        pBlk->prev = tail;
        tail->next = pBlk;
    }
    else
    {
        // 发送队列为空
        pBlk->prev = nullptr;
        pe->sendListHead = pBlk;
    }

    pe->sendListTail = pBlk;
    pe->totalBufSize += pBlk->dataSize;

    // is buffer full
    if (pe->totalBufSize >= mSetting.bufferPerSessionLimit)
    {
        // 缓冲区满
        // spdlog::trace("[TcpForwardService::onRead] soc[{}] buffer full", pe->soc);
        pe->bufferFull = true;
    }
}

void TcpForwardService::closeTunnel(UdpTunnel_t *pt)
{
    switch (pt->stat)
    {
    case TunnelState_t::BROKEN:
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
                setStatus(pt, TunnelState_t::CLOSED);
                closeTunnel(pt);
            }
        }
        else
        {
            setStatus(pt, TunnelState_t::CLOSED);
            closeTunnel(pt);
        }
    }
    break;
    case TunnelState_t::CLOSED:
        // release tunnel
        spdlog::debug("[TcpForwardService::closeTunnel] close tunnel[{}:{}]",
                      pt->south->soc, pt->north->soc);

        // remove from waiting buffer list
        if (pt->north->bufWaitEntry.inList)
        {
            spdlog::trace("[TcpForwardService::closeTunnel] remove north soc[{}]"
                          " from buffer waiting list",
                          pt->north->soc);
            mBufferWaitList.erase(&pt->north->bufWaitEntry);
        }
        if (pt->south->bufWaitEntry.inList)
        {
            spdlog::trace("[TcpForwardService::closeTunnel] remove south soc[{}]"
                          " from buffer waiting list",
                          pt->south->soc);
            mBufferWaitList.erase(&pt->south->bufWaitEntry);
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
    case TunnelState_t::INITIALIZED:
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

void TcpForwardService::processBufferWaitingList()
{
    if (mBufferWaitList.mpHead)
    {
        auto entry = mBufferWaitList.mpHead;
        while (entry)
        {
            auto pe = (Endpoint_t *)((TimerList::Entity_t *)entry)->container;
            auto pBufBlk = mpBuffer->getCurBufBlk();
            if (pBufBlk == nullptr)
            {
                // 已无空闲可用缓冲区
                break;
            }

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
                appendToSendList(pe->peer, pBlk);

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
            mBufferWaitList.erase((TimerList::Entity_t *)entry);
            entry = next;
        }
    }
}

} // namespace link
} // namespace mapper
