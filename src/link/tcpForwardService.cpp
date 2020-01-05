#include "tcpForwardService.h"
#include <time.h>
#include <sys/epoll.h>
#include <sstream>
#include <spdlog/spdlog.h>
#include "endpoint.h"
#include "tunnel.h"
#include "utils.h"

using namespace std;
using namespace mapper::buffer;
using namespace mapper::config;

namespace mapper
{
namespace link
{

/**
 * tunnel state machine:
 * 
 *       CLOSED -----> ALLOCATED -----> INITIALIZED -----> CONNECT -----> ESTABLISHED
 *         A                                 |                |               |
 *         |                                 |                |               |
 *         |                                 *--------------->|<--------------*
 *         |                                                  |
 *         |                                                  V
 *         *---------------------------------------------- BROKEN
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
    : Service("TcpForwardService"),
      mForwardCmd(nullptr),
      mpDynamicBuffer(nullptr),
      mLastActionTime(0),
      mTimeoutInterval_Conn(TIMEOUT_INTERVAL_CONN),
      mTimeoutInterval_Estb(TIMEOUT_INTERVAL_ESTB),
      mTimeoutInterval_Brok(TIMEOUT_INTERVAL_BROK)
{
    mTimer.init(nullptr);
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

    closeTunnels();
}

bool TcpForwardService::init(int epollfd,
                             std::shared_ptr<config::Forward> forward,
                             uint32_t sharedBufferCapacity)
{
    assert(Service::init(epollfd));

    Protocol_t protocol = Utils::parseProtocol(forward->protocol);
    assert(protocol == Protocol_t::TCP);

    mForwardCmd = forward;

    mServiceEndpoint.init(Protocol_t::TCP,
                          Direction_t::DIR_SOUTH,
                          Type_t::SERVICE);
    mServiceEndpoint.service = this;

    // get local address of specified interface
    if (!Utils::getIntfAddr(forward->interface.c_str(), mServiceEndpoint.ipTuple.local))
    {
        spdlog::error("[TcpForwardService::init] get address of interface[{}] fail.", forward->interface);
        return false;
    }
    mServiceEndpoint.ipTuple.local.sin_port = htons(atoi(forward->service.c_str()));

    // get local address of specified interface
    if (!Utils::getIntfAddr(forward->interface.c_str(), mServiceEndpoint.ipTuple.local))
    {
        spdlog::error("[TcpForwardService::init] get address of interface[{}] fail.", forward->interface);
        return false;
    }
    mServiceEndpoint.ipTuple.local.sin_port = htons(atoi(forward->service.c_str()));

    // create server socket
    mServiceEndpoint.ipTuple.localLen = sizeof(mServiceEndpoint.ipTuple.local);
    mServiceEndpoint.soc = Utils::createServiceSoc(Protocol_t::TCP,
                                                   &mServiceEndpoint.ipTuple.local,
                                                   mServiceEndpoint.ipTuple.localLen);
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
        spdlog::error("[TcpForwardService::init] add service endpoint[{}] into epoll fail.", forward->toStr());
        close();
        return false;
    }

    // alloc dynamic buffer
    mpDynamicBuffer =
        buffer::DynamicBuffer::allocDynamicBuffer(sharedBufferCapacity);
    if (mpDynamicBuffer == nullptr)
    {
        spdlog::error("[TcpForwardService::init] alloc dynamic buffer fail.");
        close();
        return false;
    }

    return true;
}

void TcpForwardService::setTimeout(TunnelState_t stat, const uint32_t interval)
{
    switch (stat)
    {
    case TunnelState_t::CONNECT:
        mTimeoutInterval_Conn = interval;
        break;
    case TunnelState_t::ESTABLISHED:
        mTimeoutInterval_Estb = interval;
        break;
    case TunnelState_t::BROKEN:
        mTimeoutInterval_Brok = interval;
        break;
    default:
        assert(!"invalid stat");
        break;
    }
}

void TcpForwardService::close()
{
    // close service socket
    if (mServiceEndpoint.soc > 0)
    {
        ::close(mServiceEndpoint.soc);
        mServiceEndpoint.soc = 0;
    }

    // release dynamic buffer
    if (mpDynamicBuffer)
    {
        spdlog::debug("[TcpForwardService::close] release dynamic buffer.");
        buffer::DynamicBuffer::releaseDynamicBuffer(mpDynamicBuffer);
        mpDynamicBuffer = nullptr;
    }
}

void TcpForwardService::onSoc(time_t curTime, uint32_t events, Endpoint_t *pe)
{
    if (pe->type == Type_t::SERVICE)
    {
        onServiceSoc(curTime, events, pe);
    }
    else
    {
        onNorthSoc(curTime, events, pe);
    }

    if (mLastActionTime < curTime)
    {
        scanTimeout(curTime);
        closeTunnels();

        mLastActionTime = curTime;
    }
}

void TcpForwardService::acceptClient(time_t curTime, Endpoint_t *pe)
{
    // alloc resources
    UdpTunnel_t *pt = getTunnel();
    if (pt == nullptr)
    {
        spdlog::error("[TcpForwardService::acceptClient] get tunnel fail");
        return;
    }

    if (![&]() -> bool {
            // accept client
            pt->south->ipTuple.remoteLen = sizeof(pt->south->ipTuple.remote);
            pt->south->soc = accept(pe->soc,
                                    (sockaddr *)&pt->south->ipTuple.remote,
                                    &pt->south->ipTuple.remoteLen);
            if (pt->south->soc == -1)
            {
                spdlog::error("[TcpForwardService::acceptClient] accept fail: {} - {}", errno, strerror(errno));
                return false;
            }
            spdlog::debug("[TcpForwardService::acceptClient] accept client[{}]: {}",
                          pt->south->soc, Utils::dumpSockAddr(pt->south->ipTuple.remote));

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
            if (!connect(curTime, pt))
            {
                spdlog::error("[TcpForwardService::acceptClient] connect to target fail");
                return false;
            }

            // add tunnel into epoll driver
            if (!epollAddEndpoint(pt->north, false, true, true) ||
                !epollAddEndpoint(pt->south, false, false, true))
            {
                spdlog::error("[TcpForwardService::acceptClient] add endpoints into epoll driver fail");
                return false;
            }

            return true;
        }())
    {
        setStatus(pt, TunnelState_t::BROKEN);
        addToCloseList(pt);
        return;
    }

    // add into timeout timer
    addToTimer(curTime, &pt->timer);

    spdlog::debug("[TcpForwardService::acceptClient] create tunnel [{}:{}]",
                  pt->south->soc, pt->north->soc);
}

void TcpForwardService::onServiceSoc(time_t curTime, uint32_t events, Endpoint_t *pe)
{
    if (events & (EPOLLRDHUP | EPOLLERR))
    {
        spdlog::error("[TcpForwardService::onServiceSoc] endpoint[{}]: {}{}{}{}",
                      Utils::dumpEndpoint(pe),
                      events & EPOLLIN ? "r" : "",
                      events & EPOLLOUT ? "w" : "",
                      events & EPOLLRDHUP ? "R" : "",
                      events & EPOLLERR ? "E" : "");
        return;
    }

    // Read
    if (events & EPOLLIN)
    {
        acceptClient(curTime, pe);
    }
}

void TcpForwardService::onNorthSoc(time_t curTime, uint32_t events, Endpoint_t *pe)
{
    if (events & (EPOLLRDHUP | EPOLLERR))
    {
        auto pt = (UdpTunnel_t *)pe->container;
        if (pt->stat == TunnelState_t::CONNECT && pt->north == pe)
        {
            spdlog::error(
                "[UdpForwardService::onNorthSoc] north soc[{}] connect fail",
                pe->soc);
        }
        else
        {
            spdlog::error("[TcpForwardService::onNorthSoc] endpoint[{}] broken. {}{}{}{}",
                          Utils::dumpEndpoint(pe),
                          events & EPOLLIN ? "r" : "",
                          events & EPOLLOUT ? "w" : "",
                          events & EPOLLRDHUP ? "R" : "",
                          events & EPOLLERR ? "E" : "");
            pe->valid = false;
        }

        // TODO: 1. add to close list
        // TODO: 2. skip this error check ?

        return;
    }

    // Read
    if (events & EPOLLIN)
    {
        northRead(curTime, pe);
    }
    // Write
    if (events & EPOLLOUT)
    {
        northWrite(curTime, pe);
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
                                              Direction_t::DIR_SOUTH,
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

    pt->north->ipTuple.remote = *(sockaddr_in *)&addrs->addr;
    pt->north->ipTuple.remoteLen = addrs->addrLen;

    return true;
}

bool TcpForwardService::epollAddEndpoint(Endpoint_t *pe, bool read, bool write, bool edgeTriger)
{
    // spdlog::debug("[TcpForwardService::epollAddEndpoint] endpoint[{}], read[{}], write[{}]",
    //               Endpoint::toStr(pe), read, write);

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
    //               Endpoint::toStr(pe), event.events);

    return true;
}

UdpTunnel_t *TcpForwardService::getTunnel(time_t curTime, sockaddr_in *southRemoteAddr)
{
    // auto it = mAddr2Tunnel.find(*southRemoteAddr);
    // if (it != mAddr2Tunnel.end())
    // {
    //     return it->second;
    // }

    // // create north endpoint
    // auto north = Endpoint::getEndpoint(Protocol_t::TCP, Direction_t::DIR_NORTH, Type_t::NORMAL);
    // if (north == nullptr)
    // {
    //     spdlog::error("[TcpForwardService::getTunnel] create north endpoint fail");
    //     return nullptr;
    // }
    // else
    // {
    //     // north->valid = ENDPOINT_VALID;
    //     north->service = this;
    //     north->peer = &mServiceEndpoint;

    //     // create to north socket
    //     north->soc = Utils::createSoc(Protocol_t::TCP, true);
    //     if (north->soc <= 0)
    //     {
    //         spdlog::error("[TcpForwardService::getTunnel] create north socket fail.");
    //         Endpoint::releaseEndpoint(north);
    //         return nullptr;
    //     }
    //     spdlog::debug("[TcpForwardService::getTunnel] create north socket[{}].", north->soc);

    //     // connect to host
    //     auto addrs = mTargetManager.getAddr(curTime);
    //     if (!addrs)
    //     {
    //         spdlog::error("[TcpForwardService::getTunnel] connect to north host fail.");
    //         ::close(north->soc);
    //         Endpoint::releaseEndpoint(north);
    //         return nullptr;
    //     }
    //     else if (connect(north->soc, &addrs->addr, addrs->addrLen) < 0)
    //     {
    //         // report fail
    //         mTargetManager.failReport(curTime, &addrs->addr);
    //         spdlog::error("[TcpForwardService::getTunnel] connect fail. {} - {}",
    //                       errno, strerror(errno));
    //         ::close(north->soc);
    //         Endpoint::releaseEndpoint(north);
    //         return nullptr;
    //     }

    //     // add into epoll driver
    //     if (!epollAddEndpoint(north, true, true, true))
    //     {
    //         spdlog::error("[TcpForwardService::getTunnel] add endpoint[{}] into epoll fail.", north->soc);
    //         ::close(north->soc);
    //         Endpoint::releaseEndpoint(north);
    //         return nullptr;
    //     }

    //     // save ip-tuple info
    //     socklen_t socLen;
    //     getsockname(north->soc, (sockaddr *)&north->ipTuple.l, &socLen);
    //     north->ipTuple.r = *(sockaddr_in *)&addrs->addr;
    // }

    // // create tunnel
    // auto tunnel = Tunnel::getTunnel();
    // if (tunnel == nullptr)
    // {
    //     spdlog::error("[TcpForwardService::getTunnel] create tunnel fail");
    //     ::close(north->soc);
    //     Endpoint::releaseEndpoint(north);
    //     return nullptr;
    // }
    // else
    // {
    //     tunnel->service = this;
    // }

    // // bind tunnel and endpoints
    // tunnel->north = north;
    // tunnel->south = &mServiceEndpoint;
    // north->container = tunnel;

    // // put into map
    // mAddr2Tunnel[*southRemoteAddr] = tunnel;
    // mAddr2Endpoint[*southRemoteAddr] = north;
    // mNorthSoc2SouthRemoteAddr[north->soc] = *southRemoteAddr;

    // // add to timer
    // addToTimer(curTime, &tunnel->timer);

    // spdlog::debug("[TcpForwardService::getTunnel] {}==>{}",
    //               Utils::dumpServiceEndpoint(&mServiceEndpoint, southRemoteAddr),
    //               Utils::dumpEndpoint(north));

    // return tunnel;
}

void TcpForwardService::southRead(time_t curTime, Endpoint_t *pe)
{
    while (true)
    {
        // // 按最大 TCP 数据包预申请内存
        // void *pBuf = mpDynamicBuffer->reserve(DEFAULT_RECV_BUFFER);
        // if (pBuf == nullptr)
        // {
        //     // out of memory
        //     spdlog::trace("[TcpForwardService::southRead] out of memory");
        //     return;
        // }

        // sockaddr_in addr;
        // socklen_t addrLen = sizeof(sockaddr_in);
        // int nRet = recvfrom(mServiceEndpoint.soc, pBuf, DEFAULT_RECV_BUFFER, 0, (sockaddr *)&addr, &addrLen);
        // if (nRet > 0)
        // {
        //     // 查找/分配对应 TCP tunnel
        //     auto tunnel = getTunnel(curTime, &addr);
        //     if (tunnel && tunnel->north->valid)
        //     {
        //         Endpoint::appendToSendList(tunnel->north, mpDynamicBuffer->cut(nRet));
        //         // 尝试发送
        //         northWrite(curTime, tunnel->north);
        //     }
        //     else
        //     {
        //         spdlog::error("[TcpForwardService::southRead] tunnel not valid");
        //     }
        // }
        // else if (nRet < 0)
        // {
        //     if (errno == EAGAIN)
        //     {
        //         // 此次数据接收已完毕
        //     }
        //     else
        //     {
        //         spdlog::error("[TcpForwardService::southRead] service soc[{}] fail: {}:[]",
        //                       mServiceEndpoint.soc, errno, strerror(errno));
        //         pe->valid = false;
        //     }
        //     break;
        // }
        // else
        // {
        //     spdlog::trace("[TcpForwardService::southRead] skip empty tcp packet.");
        // }
    }
}

void TcpForwardService::southWrite(time_t curTime, Endpoint_t *pe)
{
    // auto p = (DynamicBuffer::BufBlk_t *)pe->sendListHead;
    // while (p)
    // {
    //     int nRet = sendto(mServiceEndpoint.soc,
    //                       p->buffer,
    //                       p->size,
    //                       0,
    //                       (sockaddr *)&p->sockaddr,
    //                       sizeof(sockaddr_in));
    //     if (nRet < 0)
    //     {
    //         if (errno == EAGAIN)
    //         {
    //             // 此次数据接收已完毕
    //         }
    //         else
    //         {
    //             spdlog::error("[TcpForwardService::southWrite] service soc[{}] fail: {}:[]",
    //                           mServiceEndpoint.soc, errno, strerror(errno));
    //             pe->valid = false;
    //         }
    //         break;
    //     }

    //     mpDynamicBuffer->release(p);
    //     p = p->next;
    // }

    // if (p)
    // {
    //     // 还有数据包未发送
    //     pe->sendListHead = p;
    // }
    // else
    // {
    //     //已无数据包需要发送
    //     pe->sendListHead = pe->sendListTail = nullptr;
    // }
}

void TcpForwardService::northRead(time_t curTime, Endpoint_t *pe)
{
    // while (true)
    // {
    //     // 按最大 TCP 数据包预申请内存
    //     void *pBuf = mpDynamicBuffer->reserve(DEFAULT_RECV_BUFFER);
    //     if (pBuf == nullptr)
    //     {
    //         // out of memory
    //         spdlog::trace("[TcpForwardService::northRead] out of memory");
    //         return;
    //     }

    //     sockaddr_in addr;
    //     socklen_t addrLen = sizeof(sockaddr_in);
    //     int nRet = recvfrom(pe->soc, pBuf, DEFAULT_RECV_BUFFER, 0, (sockaddr *)&addr, &addrLen);
    //     if (nRet > 0)
    //     {
    //         // 判断数据包来源是否合法
    //         if (Utils::compareAddr(&addr, &pe->ipTuple.r))
    //         {
    //             // drop unknown incoming packet
    //             spdlog::trace("[TcpForwardService::northRead] drop unknown incoming packet");
    //             continue;
    //         }
    //         // 取南向地址
    //         auto it = mNorthSoc2SouthRemoteAddr.find(pe->soc);
    //         if (it == mNorthSoc2SouthRemoteAddr.end())
    //         {
    //             // drop unknown incoming packet
    //             spdlog::trace("[TcpForwardService::northRead] south addr not exist");
    //             continue;
    //         }

    //         auto pBlk = mpDynamicBuffer->cut(nRet);
    //         pBlk->sockaddr = it->second;

    //         Endpoint::appendToSendList(&mServiceEndpoint, pBlk);
    //         // 尝试发送
    //         southWrite(curTime, &mServiceEndpoint);
    //     }
    //     else if (nRet < 0)
    //     {
    //         if (errno == EAGAIN)
    //         {
    //             // 此次数据接收已完毕
    //             refreshTimer(curTime, &((UdpTunnel_t *)pe->container)->timer);
    //         }
    //         else
    //         {
    //             spdlog::error("[TcpForwardService::northRead] service soc[{}] fail: {}:[]",
    //                           pe->soc, errno, strerror(errno));
    //             pe->valid = false;
    //         }
    //         break;
    //     }
    //     else
    //     {
    //         spdlog::trace("[TcpForwardService::northRead] skip empty udp packet.");
    //     }
    // }
}

void TcpForwardService::northWrite(time_t curTime, Endpoint_t *pe)
{
    // auto p = (buffer::DynamicBuffer::BufBlk_t *)pe->sendListHead;
    // while (p)
    // {
    //     int nRet = send(pe->soc, p->buffer, p->size, 0);
    //     if (nRet < 0)
    //     {
    //         if (errno == EAGAIN)
    //         {
    //             // 此次发送窗口已关闭
    //             refreshTimer(curTime, &((UdpTunnel_t *)pe->container)->timer);
    //             break;
    //         }

    //         spdlog::debug("[TcpForwardService::northWrite] soc[{}] send fail: {} - [{}]",
    //                       pe->soc, errno, strerror(errno));

    //         pe->valid = false;

    //         // clean send buffer
    //         while (p)
    //         {
    //             mpDynamicBuffer->release(p);
    //             p = p->next;
    //         }

    //         break;
    //     }

    //     mpDynamicBuffer->release(p);
    //     p = p->next;
    // }

    // if (p == nullptr)
    // {
    //     // 数据发送完毕
    //     pe->sendListHead = pe->sendListTail = nullptr;
    // }
    // else
    // {
    //     // 还有待发送数据
    //     pe->sendListHead = p;
    // }
}

void TcpForwardService::closeTunnels()
{
    // if (!mCloseList.empty())
    // {
    //     for (auto pt : mCloseList)
    //     {
    //         spdlog::debug("[TcpForwardService::closeTunnels] remove endpoint[{}]",
    //                       Utils::dumpEndpoint(pt->north));
    //         // remove from maps
    //         int northSoc = pt->north->soc;
    //         auto &addr = mNorthSoc2SouthRemoteAddr[northSoc];
    //         mAddr2Tunnel.erase(addr);
    //         mAddr2Endpoint.erase(addr);
    //         mNorthSoc2SouthRemoteAddr.erase(northSoc);

    //         // close and release endpoint object
    //         ::close(northSoc);
    //         Endpoint::releaseEndpoint(pt->north);
    //         // release tunnel object
    //         Tunnel::releaseTunnel(pt);
    //     }

    //     mCloseList.clear();
    // }
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

} // namespace link
} // namespace mapper
