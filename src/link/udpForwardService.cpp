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

    mForwardCmd = forward;

    mServiceEndpoint.init(PROTOCOL_UDP, TO_SOUTH, TYPE_SERVICE);
    mServiceEndpoint.service = this;

    // get local address of specified interface
    if (!Utils::getIntfAddr(forward->interface.c_str(), mServiceEndpoint.conn.localAddr))
    {
        spdlog::error("[UdpForwardService::init] get address of interface[{}] fail.", forward->interface);
        return false;
    }
    mServiceEndpoint.conn.localAddr.sin_port = htons(atoi(forward->service.c_str()));

    // create server socket
    mServiceEndpoint.soc = Utils::createServiceSoc(PROTOCOL_UDP,
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
                                  PROTOCOL_UDP))
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
    if (pe->type == TYPE_SERVICE)
    {
        // Write
        if (events & EPOLLOUT)
        {
            southWrite(curTime, pe);
        }
        // Read
        if (events & EPOLLIN)
        {
            southRead(curTime, pe);
        }
    }
    else
    {
        // Write
        if (events & EPOLLOUT)
        {
            northWrite(curTime, pe);
        }
        // Read
        if (events & EPOLLIN)
        {
            northRead(curTime, pe);
        }
    }
}

void UdpForwardService::postProcess(time_t curTime)
{
    // 处理缓冲区等待队列
    processBufferWaitingList();

    // clean useless tunnels
    closeTunnels();
}

void UdpForwardService::scanTimeout(time_t curTime)
{
    time_t timeoutTime = curTime - mSetting.udpTimeout;
    list<TimerList::Entity_t *> timeoutList;
    mTimeoutTimer.getTimeoutList(timeoutTime, timeoutList);
    for (auto entity : timeoutList)
    {
        addToCloseList((Tunnel_t *)entity->container);
    }
}

Tunnel_t *UdpForwardService::getTunnel(time_t curTime, sockaddr_in *southRemoteAddr)
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
        north->peer = &mServiceEndpoint;

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
        auto addrs = mTargetManager.getAddr(curTime);
        if ([&]() {
                if (!addrs)
                {
                    spdlog::error("[UdpForwardService::getTunnel] connect to north host fail.");
                    return false;
                }
                else if (connect(north->soc, &addrs->addr, addrs->addrLen) < 0)
                {
                    // report fail
                    mTargetManager.failReport(curTime, &addrs->addr);
                    spdlog::error("[UdpForwardService::getTunnel] connect fail. {} - {}",
                                  errno, strerror(errno));
                    return false;
                }

                // add into epoll driver
                if (!epollAddEndpoint(north, true, true, true))
                {
                    spdlog::error("[UdpForwardService::getTunnel] add endpoint[{}] into epoll fail.", north->soc);
                    return false;
                }
            }())
        {
            // save ip-tuple info
            socklen_t socLen;
            getsockname(north->soc, (sockaddr *)&north->conn.localAddr, &socLen);
            north->conn.remoteAddr = *(sockaddr_in *)&addrs->addr;
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
    else
    {
        pt->service = this;
    }

    // bind tunnel and endpoints
    pt->north = north;
    pt->south = &mServiceEndpoint;
    north->container = pt;

    // put into map
    mAddr2Tunnel[*southRemoteAddr] = pt;
    mNorthSoc2SouthRemoteAddr[north->soc] = *southRemoteAddr;

    // add to timer
    mTimeoutTimer.push_back(curTime, &pt->timerEntity);

    spdlog::debug("[UdpForwardService::getTunnel] create udp tunnel: {}==>[{}]-{}",
                  Utils::dumpSockAddr(mServiceEndpoint.conn.localAddr),
                  north->soc, Utils::dumpSockAddr(north->conn.remoteAddr));

    return pt;
}

void UdpForwardService::southRead(time_t curTime, Endpoint_t *pe)
{
    if (!pe->valid)
    {
        spdlog::critical("[UdpForwardService::southRead] service soc[{}] not valid");
        return;
    }

    set<Endpoint_t *> sendList;

    while (true)
    {
        // 按最大 UDP 数据包预申请内存
        void *pBuf = mpBuffer->reserve(PREALLOC_RECV_BUFFER_SIZE);
        if (pBuf == nullptr)
        {
            // out of memory
            spdlog::trace("[UdpForwardService::southRead] out of memory");
            // 加入缓冲区等待队列
            mBufferWaitList.push_back(&pe->bufWaitEntry);
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
                sendList.insert(tunnel->north);
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

    // 尝试发送
    for (auto &pe : sendList)
    {
        northWrite(curTime, pe);
    }
    sendList.clear();
}

void UdpForwardService::southWrite(time_t curTime, Endpoint_t *pe)
{
    if (!pe->valid)
    {
        spdlog::critical("[UdpForwardService::southRead] service soc[{}] not valid");
        if (pe->sendListHead)
        {
            // clean buffer list
            auto p = (DynamicBuffer::BufBlk_t *)pe->sendListHead;
            while (p)
            {
                auto next = p->next;
                mpBuffer->release(p);
                p = next;
            }
            pe->sendListHead = pe->sendListTail = nullptr;
            pe->totalBufSize = 0;
        }
        return;
    }

    auto p = (DynamicBuffer::BufBlk_t *)pe->sendListHead;
    while (p)
    {
        auto it = mAddr2Tunnel.find(p->destAddr);
        if (it != mAddr2Tunnel.end())
        {
            // 已被移除 tunnel 的剩余数据
            spdlog::debug(!"[UdpForwardService::southWrite] drop closed tunnel pkt");
        }
        else
        {
            int nRet = sendto(mServiceEndpoint.soc,
                              p->buffer + p->sent,
                              p->dataSize - p->sent,
                              0,
                              (sockaddr *)&p->destAddr,
                              sizeof(p->destAddr));
            if (nRet > 0)
            {
                p->sent += nRet;
                if (p->sent < p->dataSize)
                {
                    // 数据包中还有数据需要发送
                    continue;
                }

                assert(p->sent == p->dataSize);
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
                    spdlog::debug("[UdpForwardService::southWrite] send to client[{}] fail: {}:[]",
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
        pe->totalBufSize -= p->dataSize;
        mpBuffer->release(p);
        p = next;
    }

    if (p)
    {
        // 还有数据包未发送
        pe->sendListHead = p;
        assert(pe->totalBufSize > 0);
    }
    else
    {
        //已无数据包需要发送
        pe->sendListHead = pe->sendListTail = nullptr;
        assert(pe->totalBufSize == 0);
    }
}

void UdpForwardService::northRead(time_t curTime, Endpoint_t *pe)
{
    if (!pe->valid)
    {
        spdlog::debug("[UdpForwardService::northRead] skip invalid soc[{}]");
        return;
    }

    while (true)
    {
        // 按最大 UDP 数据包预申请内存
        void *pBuf = mpBuffer->reserve(PREALLOC_RECV_BUFFER_SIZE);
        if (pBuf == nullptr)
        {
            // out of memory
            spdlog::trace("[UdpForwardService::northRead] out of memory");
            // 加入缓冲区等待队列
            mBufferWaitList.push_back(&pe->bufWaitEntry);
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
                spdlog::debug("[UdpForwardService::northRead] drop invalid addr[{}] pkt at soc[{}] for {}",
                              Utils::dumpSockAddr(addr),
                              pe->soc, Utils::dumpSockAddr(pe->conn.remoteAddr));
                continue;
            }

            // 取南向地址
            auto it = mNorthSoc2SouthRemoteAddr.find(pe->soc);
            if (it == mNorthSoc2SouthRemoteAddr.end())
            {
                // drop unknown incoming packet
                assert(!"[UdpForwardService::northRead] south addr not exist");
            }

            auto pBlk = mpBuffer->cut(nRet);
            pBlk->destAddr = it->second;

            Endpoint::appendToSendList(&mServiceEndpoint, pBlk);

            // refresh timer
            mTimeoutTimer.refresh(curTime, &((Tunnel_t *)pe->container)->timerEntity);
        }
        else if (nRet < 0)
        {
            if (errno == EAGAIN)
            {
                // 此次数据接收已完毕

                // refresh timer
                mTimeoutTimer.refresh(curTime, &((Tunnel_t *)pe->container)->timerEntity);
            }
            else
            {
                spdlog::error("[UdpForwardService::northRead] service soc[{}] fail: {}:[]",
                              pe->soc, errno, strerror(errno));
                pe->valid = false;

                // close client tunnel
                addToCloseList((Tunnel_t *)pe->container);
            }
            break;
        }
        else
        {
            spdlog::trace("[UdpForwardService::northRead] skip empty udp packet.");
        }
    }

    // 尝试发送
    if (mServiceEndpoint.sendListHead)
    {
        southWrite(curTime, &mServiceEndpoint);
    }
}

void UdpForwardService::northWrite(time_t curTime, Endpoint_t *pe)
{
    if (!pe->valid)
    {
        spdlog::critical("[UdpForwardService::northWrite] skip invalid soc[{}]");
        if (pe->sendListHead)
        {
            // clean buffer list
            auto p = (DynamicBuffer::BufBlk_t *)pe->sendListHead;
            while (p)
            {
                auto next = p->next;
                mpBuffer->release(p);
                p = next;
            }
            pe->sendListHead = pe->sendListTail = nullptr;
            pe->totalBufSize = 0;
        }
        return;
    }

    auto p = (buffer::DynamicBuffer::BufBlk_t *)pe->sendListHead;
    while (p)
    {
        int nRet = send(pe->soc, p->buffer, p->dataSize, 0);
        if (nRet > 0)
        {
            p->sent += nRet;
            if (p->sent < p->dataSize)
            {
                // 数据包中还有数据需要发送
                continue;
            }

            assert(p->sent == p->dataSize);
        }
        else if (nRet < 0)
        {
            if (errno == EAGAIN)
            {
                // 此次发送窗口已关闭
                mTimeoutTimer.refresh(curTime, &((Tunnel_t *)pe->container)->timerEntity);
                break;
            }

            spdlog::debug("[UdpForwardService::northWrite] soc[{}] send fail: {} - [{}]",
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

        mTimeoutTimer.refresh(curTime, &((Tunnel_t *)pe->container)->timerEntity);
        pe->totalBufSize -= p->dataSize;
        mpBuffer->release(p);
        p = p->next;
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
            spdlog::debug("[UdpForwardService::closeTunnels] close tunnel for [{}: {}]",
                          pt->north->soc, Utils::dumpSockAddr(pt->north->conn.remoteAddr));

            // remove from maps
            int northSoc = pt->north->soc;
            auto &addr = mNorthSoc2SouthRemoteAddr[northSoc];
            mAddr2Tunnel.erase(addr);
            mNorthSoc2SouthRemoteAddr.erase(northSoc);

            // remove from timer
            mTimeoutTimer.erase(&pt->timerEntity);

            // 从缓冲区等待队列中移除
            if (pt->north->bufWaitEntry.inList)
            {
                spdlog::trace("[UdpForwardService::closeTunnels] remove north soc[{}]"
                              " from buffer waiting list",
                              pt->north->soc);
                mBufferWaitList.erase(&pt->north->bufWaitEntry);
            }

            // close and release endpoint object
            ::close(northSoc);
            Endpoint::releaseEndpoint(pt->north);
            // release tunnel object
            Tunnel::releaseTunnel(pt);
        }

        mCloseList.clear();
    }
}

void UdpForwardService::processBufferWaitingList()
{
    // if (mBufferWaitList.mpHead)
    // {
    //     auto entry = mBufferWaitList.mpHead;
    //     while (entry)
    //     {
    //         auto pBufBlk = mpBuffer->getCurBufBlk();
    //         if (pBufBlk == nullptr)
    //         {
    //             // 已无空闲可用缓冲区
    //             break;
    //         }

    //         auto pe = (Endpoint_t *)entry->container;
    //         int nRet = recv(pe->soc, pBufBlk->buffer, pBufBlk->getBufSize(), 0);
    //          if (nRet > 0)
    //         {
    //             // cut buffer
    //             auto pBlk = mpBuffer->cut(nRet);
    //             // attach to peer's send list
    //             Endpoint::appendToSendList(pe->peer, pBlk);

    //             // 重置停止接收标志
    //             pe->stopRecv = false;

    //             // 重置对应会话收发事件
    //             if (!epollResetEndpointMode(pe, true, true, true) ||
    //                 !epollResetEndpointMode(pe->peer, true, true, true))
    //             {
    //                 // closed by peer
    //                 spdlog::debug("[UdpForwardService::processBufferWaitingList] soc[{}] reset fail",
    //                               pe->soc);
    //                 pe->valid = false;
    //                 addToCloseList(pe);
    //             }
    //         }
    //         else if (nRet < 0)
    //         {
    //             if (errno == EAGAIN) // 此端口的送窗口关闭 还是 有错误发生
    //             {
    //                 spdlog::debug("[UdpForwardService::processBufferWaitingList] soc[{}] EAGAIN", pe->soc);
    //             }
    //             else
    //             {
    //                 pe->valid = false;
    //                 if (pe->direction == TO_NORTH)
    //                 {
    //                     spdlog::debug("[UdpForwardService::processBufferWaitingList] soc[{}] recv fail: {} - [{}]",
    //                                   pe->soc, errno, strerror(errno));
    //                     addToCloseList(pe);
    //                 }
    //                 else
    //                 {
    //                     spdlog::critical("[UdpForwardService::processBufferWaitingList] south soc[{}] recv fail: {} - [{}]",
    //                                      pe->soc, errno, strerror(errno));
    //                 }
    //             }
    //         }
    //         else
    //         {
    //             // closed by peer
    //             spdlog::debug("[UdpForwardService::processBufferWaitingList] soc[{}] closed by peer", pe->soc);
    //             pe->valid = false;
    //             addToCloseList(pe);
    //         }

    //         // 将当前节点从等待队列中移除
    //         auto next = entry->next;
    //         mBufferWaitList.erase(entry);
    //         entry = next;
    //     }
    // }
}

} // namespace link
} // namespace mapper
