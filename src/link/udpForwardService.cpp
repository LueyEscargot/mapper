#include "udpForwardService.h"
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

UdpForwardService::UdpForwardService()
    : Service("UdpForwardService"),
      mForwardCmd(nullptr),
      mLastActionTime(0)
{
    mTimer.init(nullptr);
}

UdpForwardService::~UdpForwardService()
{
    closeTunnels();
}

bool UdpForwardService::init(int epollfd,
                             DynamicBuffer *pBuffer,
                             shared_ptr<Forward> forward,
                             Setting_t &setting)
{
    assert(Service::init(epollfd, pBuffer));

    mSetting = setting;

    Protocol_t protocol = Utils::parseProtocol(forward->protocol);
    assert(protocol == Protocol_t::UDP);

    mForwardCmd = forward;

    mServiceEndpoint.init(Protocol_t::UDP,
                          Direction_t::DIR_SOUTH,
                          Type_t::SERVICE);
    mServiceEndpoint.service = this;

    // get local address of specified interface
    if (!Utils::getIntfAddr(forward->interface.c_str(), mServiceEndpoint.conn.localAddr))
    {
        spdlog::error("[UdpForwardService::init] get address of interface[{}] fail.", forward->interface);
        return false;
    }
    mServiceEndpoint.conn.localAddr.sin_port = htons(atoi(forward->service.c_str()));

    // create server socket
    mServiceEndpoint.soc = Utils::createServiceSoc(Protocol_t::UDP,
                                                   &mServiceEndpoint.conn.localAddr,
                                                   sizeof(mServiceEndpoint.conn.localAddr));
    if (mServiceEndpoint.soc < 0)
    {
        spdlog::error("[UdpForwardService::init] create server socket fail.");
        return false;
    }

    // init target manager
    if (!mTargetManager.addTarget(time(nullptr),
                                  forward->targetHost.c_str(),
                                  forward->targetService.c_str(),
                                  Protocol_t::UDP))
    {
        spdlog::error("[UdpForwardService::init] ginit target manager fail");
        close();
        return false;
    }

    // add service's endpoint into epoll driver
    if (!epollAddEndpoint(&mServiceEndpoint, true, true, true))
    {
        spdlog::error("[UdpForwardService::init] add service endpoint[{}] into epoll fail.", forward->toStr());
        close();
        return false;
    }

    return true;
}

void UdpForwardService::close()
{
    // close service socket
    if (mServiceEndpoint.soc > 0)
    {
        ::close(mServiceEndpoint.soc);
        mServiceEndpoint.soc = 0;
    }
}

void UdpForwardService::onSoc(time_t curTime, uint32_t events, Endpoint_t *pe)
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

void UdpForwardService::postProcess(time_t curTime)
{
}

void UdpForwardService::scanTimeout(time_t curTime)
{
    time_t timeoutTime = curTime - mSetting.sessionTimeout;
    if (mTimer.next == nullptr ||
        mTimer.next->lastActiveTime > timeoutTime)
    {
        return;
    }

    // get timeout item list
    auto h = mTimer.next;
    auto t = h;
    while (t->next && t->next->lastActiveTime < timeoutTime)
    {
        t = t->next;
    }
    // 将从 h --> t 的元素移除链表
    if (t->next)
    {
        // 此时剩余链表中还有元素存在
        t->next->prev = nullptr;
        mTimer.next = t->next;
        t->next = nullptr;
    }
    else
    {
        // 所有元素都已从链表中移除
        mTimer.next = mTimer.prev = nullptr;
    }

    // 释放已超时 udp tunnel
    while (h)
    {
        addToCloseList((UdpTunnel_t *)h->tunnel);
        h = h->next;
    }
}

void UdpForwardService::onServiceSoc(time_t curTime, uint32_t events, Endpoint_t *pe)
{
    if (events & (EPOLLRDHUP | EPOLLERR))
    {
        spdlog::error("[UdpForwardService::onServiceSoc] endpoint[{}]: {}{}{}{}",
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
        southRead(curTime, pe);
    }
    // Write
    if (events & EPOLLOUT)
    {
        southWrite(curTime, pe);
    }
}

void UdpForwardService::onNorthSoc(time_t curTime, uint32_t events, Endpoint_t *pe)
{
    if (events & (EPOLLRDHUP | EPOLLERR))
    {
        spdlog::error("[UdpForwardService::onNorthSoc] endpoint[{}]: {}{}{}{}",
                      Utils::dumpEndpoint(pe),
                      events & EPOLLIN ? "r" : "",
                      events & EPOLLOUT ? "w" : "",
                      events & EPOLLRDHUP ? "R" : "",
                      events & EPOLLERR ? "E" : "");
        pe->valid = false;
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

bool UdpForwardService::epollAddEndpoint(Endpoint_t *pe, bool read, bool write, bool edgeTriger)
{
    // spdlog::debug("[UdpForwardService::epollAddEndpoint] endpoint[{}], read[{}], write[{}]",
    //               Endpoint::toStr(pe), read, write);

    struct epoll_event event;
    event.data.ptr = pe;
    event.events = EPOLLRDHUP |                // for peer close
                   (read ? EPOLLIN : 0) |      // enable read
                   (write ? EPOLLOUT : 0) |    // enable write
                   (edgeTriger ? EPOLLET : 0); // use edge triger or level triger
    if (epoll_ctl(mEpollfd, EPOLL_CTL_ADD, pe->soc, &event))
    {
        spdlog::error("[UdpForwardService::epollAddEndpoint] events[{}]-soc[{}] join fail. Error {}: {}",
                      event.events, pe->soc, errno, strerror(errno));
        return false;
    }

    // spdlog::debug("[UdpForwardService::epollAddEndpoint] endpoint[{}], event.events[0x{:X}]",
    //               Endpoint::toStr(pe), event.events);

    return true;
}

UdpTunnel_t *UdpForwardService::getTunnel(time_t curTime, sockaddr_in *southRemoteAddr)
{
    auto it = mAddr2Tunnel.find(*southRemoteAddr);
    if (it != mAddr2Tunnel.end())
    {
        return it->second;
    }

    // create north endpoint
    auto north = Endpoint::getEndpoint(Protocol_t::UDP, Direction_t::DIR_NORTH, Type_t::NORMAL);
    if (north == nullptr)
    {
        spdlog::error("[UdpForwardService::getTunnel] create north endpoint fail");
        return nullptr;
    }
    else
    {
        // north->valid = ENDPOINT_VALID;
        north->service = this;
        north->peer = &mServiceEndpoint;

        // create to north socket
        north->soc = Utils::createSoc(Protocol_t::UDP, true);
        if (north->soc <= 0)
        {
            spdlog::error("[UdpForwardService::getTunnel] create north socket fail.");
            Endpoint::releaseEndpoint(north);
            return nullptr;
        }
        spdlog::debug("[UdpForwardService::getTunnel] create north socket[{}].", north->soc);

        // connect to host
        auto addrs = mTargetManager.getAddr(curTime);
        if (!addrs)
        {
            spdlog::error("[UdpForwardService::getTunnel] connect to north host fail.");
            ::close(north->soc);
            Endpoint::releaseEndpoint(north);
            return nullptr;
        }
        else if (connect(north->soc, &addrs->addr, addrs->addrLen) < 0)
        {
            // report fail
            mTargetManager.failReport(curTime, &addrs->addr);
            spdlog::error("[UdpForwardService::getTunnel] connect fail. {} - {}",
                          errno, strerror(errno));
            ::close(north->soc);
            Endpoint::releaseEndpoint(north);
            return nullptr;
        }

        // add into epoll driver
        if (!epollAddEndpoint(north, true, true, true))
        {
            spdlog::error("[UdpForwardService::getTunnel] add endpoint[{}] into epoll fail.", north->soc);
            ::close(north->soc);
            Endpoint::releaseEndpoint(north);
            return nullptr;
        }

        // save ip-tuple info
        socklen_t socLen;
        getsockname(north->soc, (sockaddr *)&north->conn.localAddr, &socLen);
        north->conn.remoteAddr = *(sockaddr_in *)&addrs->addr;
    }

    // create tunnel
    auto tunnel = Tunnel::getTunnel();
    if (tunnel == nullptr)
    {
        spdlog::error("[UdpForwardService::getTunnel] create tunnel fail");
        ::close(north->soc);
        Endpoint::releaseEndpoint(north);
        return nullptr;
    }
    else
    {
        tunnel->service = this;
    }

    // bind tunnel and endpoints
    tunnel->north = north;
    tunnel->south = &mServiceEndpoint;
    north->container = tunnel;

    // put into map
    mAddr2Tunnel[*southRemoteAddr] = tunnel;
    mAddr2Endpoint[*southRemoteAddr] = north;
    mNorthSoc2SouthRemoteAddr[north->soc] = *southRemoteAddr;

    // add to timer
    addToTimer(curTime, &tunnel->timer);

    spdlog::debug("[UdpForwardService::getTunnel] {}==>{}",
                  Utils::dumpServiceEndpoint(&mServiceEndpoint, southRemoteAddr),
                  Utils::dumpEndpoint(north));

    return tunnel;
}

void UdpForwardService::southRead(time_t curTime, Endpoint_t *pe)
{
    while (true)
    {
        // 按最大 UDP 数据包预申请内存
        void *pBuf = mpBuffer->reserve(PREALLOC_RECV_BUFFER_SIZE);
        if (pBuf == nullptr)
        {
            // out of memory
            spdlog::trace("[UdpForwardService::southRead] out of memory");
            return;
        }

        sockaddr_in addr;
        socklen_t addrLen = sizeof(sockaddr_in);
        int nRet = recvfrom(mServiceEndpoint.soc, pBuf, PREALLOC_RECV_BUFFER_SIZE, 0, (sockaddr *)&addr, &addrLen);
        if (nRet > 0)
        {
            // 查找/分配对应 UDP tunnel
            auto tunnel = getTunnel(curTime, &addr);
            if (tunnel && tunnel->north->valid)
            {
                Endpoint::appendToSendList(tunnel->north, mpBuffer->cut(nRet));
                // 尝试发送
                northWrite(curTime, tunnel->north);
            }
            else
            {
                spdlog::error("[UdpForwardService::southRead] tunnel not valid");
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
                spdlog::error("[UdpForwardService::southRead] service soc[{}] fail: {}:[]",
                              mServiceEndpoint.soc, errno, strerror(errno));
                pe->valid = false;
            }
            break;
        }
        else
        {
            spdlog::trace("[UdpForwardService::southRead] skip empty udp packet.");
        }
    }
}

void UdpForwardService::southWrite(time_t curTime, Endpoint_t *pe)
{
    auto p = (DynamicBuffer::BufBlk_t *)pe->sendListHead;
    while (p)
    {
        int nRet = sendto(mServiceEndpoint.soc,
                          p->buffer,
                          p->size,
                          0,
                          (sockaddr *)&p->sockaddr,
                          sizeof(sockaddr_in));
        if (nRet < 0)
        {
            if (errno == EAGAIN)
            {
                // 此次数据接收已完毕
            }
            else
            {
                spdlog::error("[UdpForwardService::southWrite] service soc[{}] fail: {}:[]",
                              mServiceEndpoint.soc, errno, strerror(errno));
                pe->valid = false;
            }
            break;
        }

        mpBuffer->release(p);
        p = p->next;
    }

    if (p)
    {
        // 还有数据包未发送
        pe->sendListHead = p;
    }
    else
    {
        //已无数据包需要发送
        pe->sendListHead = pe->sendListTail = nullptr;
    }
}

void UdpForwardService::northRead(time_t curTime, Endpoint_t *pe)
{
    while (true)
    {
        // 按最大 UDP 数据包预申请内存
        void *pBuf = mpBuffer->reserve(PREALLOC_RECV_BUFFER_SIZE);
        if (pBuf == nullptr)
        {
            // out of memory
            spdlog::trace("[UdpForwardService::northRead] out of memory");
            return;
        }

        sockaddr_in addr;
        socklen_t addrLen = sizeof(sockaddr_in);
        int nRet = recvfrom(pe->soc, pBuf, PREALLOC_RECV_BUFFER_SIZE, 0, (sockaddr *)&addr, &addrLen);
        if (nRet > 0)
        {
            // 判断数据包来源是否合法
            if (Utils::compareAddr(&addr, &pe->conn.remoteAddr))
            {
                // drop unknown incoming packet
                spdlog::trace("[UdpForwardService::northRead] drop unknown incoming packet");
                continue;
            }
            // 取南向地址
            auto it = mNorthSoc2SouthRemoteAddr.find(pe->soc);
            if (it == mNorthSoc2SouthRemoteAddr.end())
            {
                // drop unknown incoming packet
                spdlog::trace("[UdpForwardService::northRead] south addr not exist");
                continue;
            }

            auto pBlk = mpBuffer->cut(nRet);
            pBlk->sockaddr = it->second;

            Endpoint::appendToSendList(&mServiceEndpoint, pBlk);
            // 尝试发送
            southWrite(curTime, &mServiceEndpoint);
        }
        else if (nRet < 0)
        {
            if (errno == EAGAIN)
            {
                // 此次数据接收已完毕
                refreshTimer(curTime, &((UdpTunnel_t *)pe->container)->timer);
            }
            else
            {
                spdlog::error("[UdpForwardService::northRead] service soc[{}] fail: {}:[]",
                              pe->soc, errno, strerror(errno));
                pe->valid = false;
            }
            break;
        }
        else
        {
            spdlog::trace("[UdpForwardService::northRead] skip empty udp packet.");
        }
    }
}

void UdpForwardService::northWrite(time_t curTime, Endpoint_t *pe)
{
    auto p = (buffer::DynamicBuffer::BufBlk_t *)pe->sendListHead;
    while (p)
    {
        int nRet = send(pe->soc, p->buffer, p->size, 0);
        if (nRet < 0)
        {
            if (errno == EAGAIN)
            {
                // 此次发送窗口已关闭
                refreshTimer(curTime, &((UdpTunnel_t *)pe->container)->timer);
                break;
            }

            spdlog::debug("[UdpForwardService::northWrite] soc[{}] send fail: {} - [{}]",
                          pe->soc, errno, strerror(errno));

            pe->valid = false;

            // clean send buffer
            while (p)
            {
                mpBuffer->release(p);
                p = p->next;
            }

            break;
        }

        mpBuffer->release(p);
        p = p->next;
    }

    if (p == nullptr)
    {
        // 数据发送完毕
        pe->sendListHead = pe->sendListTail = nullptr;
    }
    else
    {
        // 还有待发送数据
        pe->sendListHead = p;
    }
}

void UdpForwardService::closeTunnels()
{
    if (!mCloseList.empty())
    {
        for (auto pt : mCloseList)
        {
            spdlog::debug("[UdpForwardService::closeTunnels] remove endpoint[{}]",
                          Utils::dumpEndpoint(pt->north));
            // remove from maps
            int northSoc = pt->north->soc;
            auto &addr = mNorthSoc2SouthRemoteAddr[northSoc];
            mAddr2Tunnel.erase(addr);
            mAddr2Endpoint.erase(addr);
            mNorthSoc2SouthRemoteAddr.erase(northSoc);

            // close and release endpoint object
            ::close(northSoc);
            Endpoint::releaseEndpoint(pt->north);
            // release tunnel object
            Tunnel::releaseTunnel(pt);
        }

        mCloseList.clear();
    }
}

void UdpForwardService::addToTimer(time_t curTime, TunnelTimer_t *p)
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

void UdpForwardService::refreshTimer(time_t curTime, TunnelTimer_t *p)
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

} // namespace link
} // namespace mapper
