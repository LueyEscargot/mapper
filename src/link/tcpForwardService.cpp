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

namespace mapper
{
namespace link
{

/**
 * tunnel state machine:
 *                                 |
 *       CLOSED -----> ALLOCATED --|--> INITIALIZED -----> CONNECT -----> ESTABLISHED
 *                         A       |         |                |               |
 *                         |       |         |                |               |
 *                         |       |         *--------------->|<--------------*
 *                         |       |                          |
 *                         |       |                          V
 *                         *-------|--- CLOSED <----------- BROKEN
 *                                 |
 */
const bool TcpForwardService::StateMaine[TUNNEL_STATE_COUNT][TUNNEL_STATE_COUNT] = {
    // CLOSED | ALLOCATED | INITIALIZED | CONNECT | ESTABLISHED | BROKEN
    {1, 1, 0, 0, 0, 0}, // CLOSED
    {0, 1, 1, 0, 0, 0}, // ALLOCATED
    {0, 0, 1, 1, 0, 1}, // INITIALIZED
    {0, 0, 0, 1, 1, 1}, // CONNECT
    {0, 0, 0, 0, 1, 1}, // ESTABLISHED
    {1, 0, 0, 0, 0, 1}, // BROKEN
};

TcpForwardService::TcpForwardService()
    : Service("TcpForwardService")
{
    mTimer.init(nullptr);
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
    if (!epollAddEndpoint(&mServiceEndpoint, true, true, true))
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
                            epollAddEndpoint(pt->south, true, true, true))
                        {
                            setStatus(pt, TunnelState_t::ESTABLISHED);

                            spdlog::debug("[TcpForwardService::onSoc] tunnel[{},{}] established.",
                                          pt->south->soc, pt->north->soc);

                            // TODO: 切换定时器
                            // mTimer.remove(&pt->timerClient);
                            // // insert into established timer container
                            // mTimer.insert(timer::Container::Type_t::TIMER_ESTABLISHED,
                            //               curTime,
                            //               &pt->timerClient);}
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
        }
        else
        {
            // to south socket

            assert(pe->direction == Direction_t::DIR_SOUTH);

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

            // Write
            if ((events & EPOLLOUT) && pe->sendListHead)
            {
                onWrite(curTime, pe);
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
            case TunnelState_t::ESTABLISHED:
                spdlog::debug("[TcpForwardService::postProcess] remove tunnel[{}:{}]",
                              pt->south->soc, pt->north->soc);
                // set tunnel status to broken
                setStatus(pt, TunnelState_t::BROKEN);
                // TODO: switch timeout container
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
    // time_t timeoutTime = curTime - mTimeoutInterval;
    // if (mTimer.next == nullptr ||
    //     mTimer.next->lastActiveTime > timeoutTime)
    // {
    //     return;
    // }

    // // get timeout item list
    // auto h = mTimer.next;
    // auto t = h;
    // while (t->next && t->next->lastActiveTime < timeoutTime)
    // {
    //     t = t->next;
    // }
    // // 将从 h --> t 的元素移除链表
    // if (t->next)
    // {
    //     // 此时剩余链表中还有元素存在
    //     t->next->prev = nullptr;
    //     mTimer.next = t->next;
    //     t->next = nullptr;
    // }
    // else
    // {
    //     // 所有元素都已从链表中移除
    //     mTimer.next = mTimer.prev = nullptr;
    // }

    // // 释放已超时 udp tunnel
    // while (h)
    // {
    //     addToCloseList((UdpTunnel_t *)h->tunnel);
    //     h = h->next;
    // }
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

        spdlog::critical("[TcpForwardService::setSTatus] invalid status convert: {} --> {}.",
                         pt->stat, stat);
        assert(false);
    }

    spdlog::trace("[TcpForwardService::setSTatus] stat: {} --> {}.", pt->stat, stat);
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
    while (true)
    {
        // alloc resources
        UdpTunnel_t *pt = getTunnel();
        if (pt == nullptr)
        {
            spdlog::error("[TcpForwardService::acceptClient] out of tunnel");

            // reject new clients
            int soc = accept(pe->soc, nullptr, nullptr);
            (soc > 0) && ::close(soc);

            continue;
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
                if (!epollAddEndpoint(pt->north, false, true, true))
                {
                    spdlog::error("[TcpForwardService::acceptClient] add endpoints into epoll driver fail");
                    return false;
                }

                return true;
            }())
        {
            if (errno == EAGAIN)
            {
                return;
            }
            else
            {
                addToCloseList(pt);
                continue;
            };
        }

        // add into timeout timer
        addToTimer(curTime, &pt->timer);

        spdlog::debug("[TcpForwardService::acceptClient] create tunnel[{}:{}]",
                      pt->south->soc, pt->north->soc);
    }
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
    if (isInBufferWaitingList(pe))
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
        if (pe->peer->sendListTotalSize >= mSetting.bufferPerSessionLimit)
        {
            // 缓冲区满
            spdlog::trace("[TcpForwardService::onRead] soc[{}] buffer full",
                          pe->soc);
            pe->stopRecv = true;
            break;
        }

        // 按最大 TCP 数据包预申请内存
        char *buf = mpBuffer->reserve(PREALLOC_RECV_BUFFER_SIZE);
        if (buf == nullptr)
        {
            if (events & EPOLLRDHUP)
            {
                // peer stop send
                spdlog::debug("[TcpForwardService::onRead] soc[{}] peer stop send");
                pe->valid = false;
                addToCloseList(pt);
            }
            else
            {
                // out of buffer
                // spdlog::trace("[TcpForwardService::onRead] soc[{}] out of buffer", pe->soc);
                pe->stopRecv = true;

                // append into buffer waiting list
                addToBufferWaitingList(curTime, pe);

                break;
            }
        }

        int nRet = recv(pe->soc, buf, PREALLOC_RECV_BUFFER_SIZE, 0);
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
        pe->peer->sendListTotalSize += nRet;
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
    bool isCurrentBufFull = (pe->sendListTotalSize >= mSetting.bufferPerSessionLimit);
    auto pkt = (DynamicBuffer::BufBlk_t *)pe->sendListHead;
    while (pkt)
    {
        // send data
        assert(pkt->size >= pkt->sent);
        int nRet = send(pe->soc, pkt->buffer + pkt->sent, pkt->size - pkt->sent, 0);
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

        pkt->sent += nRet;
        pe->sendListTotalSize -= nRet;
        assert(pe->sendListTotalSize >= 0);

        if (pkt->size == pkt->sent)
        {
            // 数据包发送完毕，可回收
            auto next = pkt->next;
            mpBuffer->release(pkt);
            pkt = next;
            pktReleased = true;
        }
    }
    if (pkt == nullptr)
    {
        // 发送完毕
        pe->sendListHead = pe->sendListTail = nullptr;
        assert(pe->sendListTotalSize == 0);
    }
    else
    {
        // 还有数据需要发送
        pe->sendListHead = pkt;
        assert(pe->sendListTotalSize > 0);
    }

    // 是否有缓冲区对象被释放，已有能力接收从南向来的数据
    if (pktReleased &&
        pe->valid &&                              // 此节点有能力发送
        pt->stat == TunnelState_t::ESTABLISHED && // 只在链路建立的状态下接收来自对端的数据
        pe->peer->stopRecv &&                     // 对端正处于停止接收状态
        isCurrentBufFull)                         // 是因为缓冲区满而停止
    {
        if (epollResetEndpointMode(pe->peer, true, true, true))
        {
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
}

void TcpForwardService::closeTunnel(UdpTunnel_t *pt)
{
    if (pt->stat == TunnelState_t::BROKEN)
    {
        if ((pt->north->sendListHead == nullptr || !pt->north->valid) &&
            (pt->south->sendListHead == nullptr || !pt->south->valid))
        {
            // release tunnel

            spdlog::debug("[TcpForwardService::closeTunnel] close tunnel[{}:{}]",
                          pt->south->soc, pt->north->soc);

            // remove from waiting buffer list
            if (isInBufferWaitingList(pt->north))
            {
                printf("remove soc[%d] from buffer waiting list\n", pt->north->soc);
                removeFromWaitingList(pt->north);
            }
            if (isInBufferWaitingList(pt->south))
            {
                printf("remove soc[%d] from buffer waiting list\n", pt->south->soc);
                removeFromWaitingList(pt->south);
            }

            // TODO: remove from timer container

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
        }
        else
        {
            Endpoint_t *pe = (pt->north->sendListHead && pt->north->valid)
                                 ? pt->north
                                 : pt->south;

            // send last data to south
            if (!epollResetEndpointMode(pe, false, true, true))
            {
                spdlog::error("[TcpForwardService::closeTunnel] reset sock[{}] in epoll fail. {} - {}",
                              pe->soc, errno, strerror(errno));
                pe->valid = false;
                closeTunnel(pt);
            }
        }
    }
    else if (pt->stat == TunnelState_t::INITIALIZED)
    {
        // release tunnel
        epollRemoveTunnel(pt);

        // release objects
        Endpoint::releaseEndpoint(pt->north);
        Endpoint::releaseEndpoint(pt->south);
        Tunnel::releaseTunnel(pt);

        spdlog::debug("[TcpForwardService::closeTunnel] close tunnel[{}:{}]",
                      pt->south->soc, pt->north->soc);
    }
    else
    {
        spdlog::critical("[TcpForwardService::closeTunnel] invalid tunnel status: {}", pt->stat);
        assert(false);
    }
}

void TcpForwardService::addToTimer(time_t curTime, TunnelTimer_t *p)
{
    p->lastActiveTime = curTime;
    p->next = nullptr;

    if (mTimer.next)
    {
        // 当前链表不为空
        p->prev = mTimer.prev;
        assert(mTimer.prev->next == nullptr);
        mTimer.prev->next = p;
        mTimer.prev = p;
    }
    else
    {
        // 当前链表为空
        p->prev = nullptr;
        mTimer.next = mTimer.prev = p;
    }
}

void TcpForwardService::refreshTimer(time_t curTime, TunnelTimer_t *p)
{
    if (p->lastActiveTime == curTime ||
        mTimer.prev->lastActiveTime == p->lastActiveTime)
    {
        return;
    }

    // remove from list
    if (p->prev)
    {
        p->prev->next = p->next;
    }
    else
    {
        assert(mTimer.next == p);
        mTimer.next = p->next;
    }
    assert(p->next);
    p->next->prev = p->prev;

    // append to tail
    addToTimer(curTime, p);
}

void TcpForwardService::addToBufferWaitingList(time_t curTime, Endpoint_t *pe)
{
    printf("[TcpForwardService::addToBufferWaitingList] add endpoint: %p\n", pe);

    if (isInBufferWaitingList(pe))
    {
        // 已在队列中
        return;
    }

    pe->waitBufferTime = curTime;
    if (mBufferWaitList.waitBufferPrev == nullptr)
    {
        // 当前等待链表为空
        assert(mBufferWaitList.waitBufferNext == nullptr);
        mBufferWaitList.waitBufferNext = mBufferWaitList.waitBufferPrev = pe;
    }
    else
    {
        // 将当前节点加入链表最后
        assert(mBufferWaitList.waitBufferPrev->waitBufferNext == nullptr);

        pe->waitBufferPrev = mBufferWaitList.waitBufferPrev;
        mBufferWaitList.waitBufferPrev->waitBufferNext = pe;
    }
}

void TcpForwardService::removeFromWaitingList(Endpoint_t *pe)
{
    printf("[TcpForwardService::removeFromWaitingList] remove endpoint: %p\n", pe);

    // 从链表中移除当前节点
    if (pe->waitBufferPrev)
    {
        pe->waitBufferPrev->waitBufferNext = pe->waitBufferNext;
        pe->waitBufferPrev = nullptr;
    }
    else
    {
        // 当前节点是链表中第一个节点，
        // 调整 mBufferWaitList 中指向链表中第一个元素的指针
        mBufferWaitList.waitBufferNext = pe->waitBufferNext;
    }

    if (pe->waitBufferNext)
    {
        pe->waitBufferNext->waitBufferPrev = pe->waitBufferPrev;
        pe->waitBufferNext = nullptr;
    }
    else
    {
        // 当前节点是链表中最后一个节点，
        // 调整 mBufferWaitList 中指向链表中最后一个元素的指针
        mBufferWaitList.waitBufferPrev = pe->waitBufferPrev;
    }
}

void TcpForwardService::processBufferWaitingList()
{
    auto pe = mBufferWaitList.waitBufferNext;
    while (pe)
    {
        printf("[TcpForwardService::processBufferWaitingList] process endpoint: %p\n", pe);

        auto pBuf = mpBuffer->reserve(PREALLOC_RECV_BUFFER_SIZE);
        if (pBuf == nullptr)
        {
            // 已无空闲可用缓冲区
            printf("[TcpForwardService::processBufferWaitingList] skip endpoint[%d]: %p\n",
                   pe->soc, pe);

            break;
        }

        int nRet = recv(pe->soc, pBuf, PREALLOC_RECV_BUFFER_SIZE, 0);
        if (nRet < 0)
        {
            if (errno == EAGAIN)
            {
                // 此端口在此次发送窗口已关闭
                pe = pe->waitBufferNext;
            }
            else
            {
                spdlog::debug("[TcpForwardService::processBufferWaitingList] soc[{}] recv fail: {} - [{}]",
                              pe->soc, errno, strerror(errno));
                pe->valid = false;
                addToCloseList(pe);

                // 将当前节点从等待队列中移除
                auto next = pe->waitBufferNext;
                removeFromWaitingList(pe);
                pe = next;

                printf("[TcpForwardService::processBufferWaitingList] remove 1 endpoint: %p\n", pe);
            }
        }
        else if (nRet == 0)
        {
            // closed by peer
            spdlog::debug("[TcpForwardService::processBufferWaitingList] soc[{}] closed by peer", pe->soc);
            pe->valid = false;
            addToCloseList(pe);

            // 将当前节点从等待队列中移除
            auto next = pe->waitBufferNext;
            removeFromWaitingList(pe);
            pe = next;

            printf("[TcpForwardService::processBufferWaitingList] remove 2 endpoint: %p\n", pe);
        }
        else
        {
            // cut buffer
            auto pBlk = mpBuffer->cut(nRet);
            // attach to peer's send list
            appendToSendList(pe->peer, pBlk);
            pe->peer->sendListTotalSize += nRet;

            // 重置停止接收标志
            pe->stopRecv = false;

            // 重置对应会话收发事件
            if (!epollResetEndpointMode(pe, true, true, true) ||
                !epollResetEndpointMode(pe->peer, true, true, true))
            {
                // closed by peer
                spdlog::debug("[TcpForwardService::processBufferWaitingList] tunnel[{}:{}] reset fail",
                              pe->soc, pe->peer->soc);
                pe->valid = false;
                addToCloseList(pe);
            }

            printf("[TcpForwardService::processBufferWaitingList] remove 3 endpoint: %p\n", pe);

            // 将当前节点从等待队列中移除
            auto next = pe->waitBufferNext;
            removeFromWaitingList(pe);
            pe = next;
        }
    }
}

} // namespace link
} // namespace mapper
