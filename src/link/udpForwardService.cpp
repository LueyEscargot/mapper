#include "udpForwardService.h"
// #include <assert.h>
// #include <fcntl.h>
// #include <ifaddrs.h>
// #include <netdb.h>
// #include <unistd.h>
// #include <arpa/inet.h>
// #include <sys/types.h>
#include <sys/epoll.h>
#include <sstream>
#include <spdlog/spdlog.h>
// #include "endpoint.h"
// #include "udpTunnel.h"
// #include "../buffer/buffer.h"
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

UdpForwardService::UdpForwardService()
    : Service("UdpForwardService"),
      mForwardCmd(nullptr),
      mpDynamicBuffer(nullptr)
{
}

UdpForwardService::~UdpForwardService()
{
}

bool UdpForwardService::init(int epollfd,
                             std::shared_ptr<config::Forward> forward,
                             uint32_t sharedBufferCapacity)
{
    assert(Service::init(epollfd));

    link::Protocol_t protocol =
        strcasecmp(forward->protocol.c_str(), "tcp") == 0
            ? link::Protocol_t::TCP
            : link::Protocol_t::UDP;
    assert(protocol == Protocol_t::UDP);

    mForwardCmd = forward;

    mServiceEndpoint.init();
    mServiceEndpoint.direction = ENDPOINT_DIRECTION_SOUTH;
    mServiceEndpoint.type = ENDPOINT_TYPE_SERVICE;
    mServiceEndpoint.valid = ENDPOINT_VALID;
    mServiceEndpoint.protocol = ENDPOINT_PROTOCOL_UDP;
    mServiceEndpoint.sockAddrLen = sizeof(struct sockaddr_in);
    mServiceEndpoint.service = this;

    // get local address of specified interface
    if (!Utils::getIntfAddr(forward->interface.c_str(), mServiceEndpoint.sockAddr))
    {
        spdlog::error("[UdpForwardService::init] get address of interface[{}] fail.", forward->interface);
        return false;
    }
    mServiceEndpoint.sockAddr.sin_port = htons(atoi(forward->service.c_str()));

    // create server socket
    mServiceEndpoint.soc = Utils::createServiceSoc(Protocol_t::UDP,
                                                   &mServiceEndpoint.sockAddr,
                                                   sizeof(mServiceEndpoint.sockAddr));
    if (mServiceEndpoint.soc < 0)
    {
        spdlog::error("[UdpForwardService::init] create server socket fail.");
        return false;
    }

    // get target addrs
    if (!Utils::getAddrInfo(forward->targetHost.c_str(),
                            forward->targetService.c_str(),
                            Protocol_t::UDP,
                            &mServiceEndpoint.targetHostAddrs))
    {
        spdlog::error("[UdpForwardService::init] get addr of host[{}:{}] fail",
                      forward->targetHost, forward->targetService);
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

    // alloc dynamic buffer
    mpDynamicBuffer =
        buffer::DynamicBuffer::allocDynamicBuffer(sharedBufferCapacity);
    if (mpDynamicBuffer == nullptr)
    {
        spdlog::error("[UdpForwardService::init] alloc dynamic buffer fail.");
        close();
        return false;
    }

    return true;
}

void UdpForwardService::close()
{
    // release target addrs
    if (mServiceEndpoint.targetHostAddrs)
    {
        Utils::closeAddrInfo(mServiceEndpoint.targetHostAddrs);
        mServiceEndpoint.targetHostAddrs = nullptr;
    }

    // close service socket
    if (mServiceEndpoint.soc > 0)
    {
        ::close(mServiceEndpoint.soc);
        mServiceEndpoint.soc = 0;
    }

    // release dynamic buffer
    if (mpDynamicBuffer)
    {
        spdlog::debug("[UdpForwardService::close] release dynamic buffer.");
        buffer::DynamicBuffer::releaseDynamicBuffer(mpDynamicBuffer);
        mpDynamicBuffer = nullptr;
    }
}

void UdpForwardService::onSoc(time_t curTime, uint32_t events, Endpoint_t *pe)
{
    pe->isService()
        ? onServiceSoc(curTime, events, pe)
        : onNorthSoc(curTime, events, pe);
}

void UdpForwardService::onServiceSoc(time_t curTime, uint32_t events, Endpoint_t *pe)
{
    if (events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR))
    {
        // connection broken
        stringstream ss;
        if (events & EPOLLRDHUP)
        {
            ss << "closed by peer;";
        }
        if (events & EPOLLHUP)
        {
            ss << "hang up;";
        }
        if (events & EPOLLERR)
        {
            ss << "error;";
        }

        pe->valid = false;
        spdlog::error("[NetMgr::onSoc] service endpoint[{}] error: {}", ss.str());
        return;
    }

    // Read
    if (events & EPOLLIN)
    {
        southRead(pe);
    }
    // Write
    if (events & EPOLLOUT)
    {
        southWrite(pe);
    }
}

void UdpForwardService::onNorthSoc(time_t curTime, uint32_t events, Endpoint_t *pe)
{
    if (events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR))
    {
        // connection broken
        stringstream ss;
        if (events & EPOLLRDHUP)
        {
            ss << "closed by peer;";
        }
        if (events & EPOLLHUP)
        {
            ss << "hang up;";
        }
        if (events & EPOLLERR)
        {
            ss << "error;";
        }

        spdlog::error("[UdpForwardService::onNorthSoc] service endpoint[{}] error: {}", pe->soc, ss.str());
        pe->valid = false;
        return;
    }

    // Read
    if (events & EPOLLIN)
    {
        northRead(pe);
    }
    // Write
    if (events & EPOLLOUT)
    {
        northWrite(pe);
    }
}

bool UdpForwardService::epollAddEndpoint(link::Endpoint_t *pe, bool read, bool write, bool edgeTriger)
{
    // spdlog::debug("[UdpForwardService::epollAddEndpoint] endpoint[{}], read[{}], write[{}]",
    //               link::Endpoint::toStr(pe), read, write);

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
    //               link::Endpoint::toStr(pe), event.events);

    return true;
}

UdpTunnel_t *UdpForwardService::getTunnel(sockaddr_in *pSAI)
{
    auto it = mAddr2Tunnel.find(*pSAI);
    if (it != mAddr2Tunnel.end())
    {
        return it->second;
    }

    // create north endpoint
    auto north = Endpoint::getEndpoint();
    if (north == nullptr)
    {
        spdlog::error("[UdpForwardService::getTunnel] create north endpoint fail");
        return nullptr;
    }

    // create tunnel
    auto tunnel = Tunnel::getTunnel();
    if (tunnel == nullptr)
    {
        spdlog::error("[UdpForwardService::getTunnel] create tunnel fail");
        Endpoint::releaseEndpoint(north);
        return nullptr;
    }

    // init north endpoint
    {
        north->direction = ENDPOINT_DIRECTION_NORTH;
        north->type = ENDPOINT_TYPE_NORMAL;
        north->valid = ENDPOINT_VALID;
        north->protocol = ENDPOINT_PROTOCOL_UDP;
        north->service = this;
        north->peer = &mServiceEndpoint;

        // create to north socket
        north->soc = Utils::createSoc(Protocol_t::UDP, true);
        if (north->soc <= 0)
        {
            spdlog::error("[UdpForwardService::getTunnel] create north socket fail.");
            Endpoint::releaseEndpoint(north);
            Tunnel::releaseTunnel(tunnel);
            return nullptr;
        }
        auto p = mServiceEndpoint.targetHostAddrs;
        while (p)
        {
            if (connect(north->soc, p->ai_addr, p->ai_addrlen) < 0)
            {
                p = p->ai_next;
                if (p)
                {
                    spdlog::debug("[UdpForwardService::getTunnel] connect fail, try next address. {} - {}",
                                  errno, strerror(errno));
                    continue;
                }
                else
                {
                    spdlog::error("[UdpForwardService::getTunnel] connect fail. {} - {}",
                                  errno, strerror(errno));
                    break;
                }
            }
            break;
        }
        if (p == nullptr)
        {
            spdlog::error("[UdpForwardService::getTunnel] connect to north host fail.");
            Endpoint::releaseEndpoint(north);
            Tunnel::releaseTunnel(tunnel);
            return nullptr;
        }

        // add into epoll driver
        if (!epollAddEndpoint(north, true, true, true))
        {
            spdlog::error("[UdpForwardService::getTunnel] add endpoint[{}] into epoll fail.", north->soc);
            Endpoint::releaseEndpoint(north);
            Tunnel::releaseTunnel(tunnel);
            return nullptr;
        }
    }

    // init tunnel
    {
        tunnel->north = north;
        tunnel->south = &mServiceEndpoint;
        tunnel->service = this;
        // TODO: Tunnel::setStatus()
        tunnel->status = TunnelState_t::CONNECT;
    }
    // put into map
    mAddr2Tunnel[*pSAI] = tunnel;

    return tunnel;
}

void UdpForwardService::southRead(link::Endpoint_t *pe)
{
    while (true)
    {
        // 按最大 UDP 数据包预申请内存
        void *pBuf = mpDynamicBuffer->reserve(MAX_UDP_BUFFER);
        if (pBuf == nullptr)
        {
            // out of memory
            spdlog::trace("[UdpForwardService::southRead] out of memory");
            return;
        }

        sockaddr_in addr;
        socklen_t addrLen = sizeof(sockaddr_in);
        int nRet = recvfrom(mServiceEndpoint.soc, pBuf, MAX_UDP_BUFFER, 0, (sockaddr *)&addr, &addrLen);
        if (nRet > 0)
        {
            // 查找/分配对应 UDP tunnel
            auto tunnel = getTunnel(&addr);
            if (tunnel && tunnel->north->valid)
            {
                Endpoint::appendToSendList(tunnel->north, mpDynamicBuffer->cut(nRet));
                // 尝试发送
                northWrite(tunnel->north);
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

void UdpForwardService::southWrite(link::Endpoint_t *pe)
{
    auto p = static_cast<DynamicBuffer::BufBlk_t *>(pe->sendListHead);
    while (p)
    {
        int nRet = sendto(mServiceEndpoint.soc,
                          p->buffer,
                          p->size,
                          0,
                          (sockaddr *)&p->sockaddr,
                          p->sockaddr_len);
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

        mpDynamicBuffer->release(p);
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

void UdpForwardService::northRead(link::Endpoint_t *pe)
{
    while (true)
    {
        // 按最大 UDP 数据包预申请内存
        void *pBuf = mpDynamicBuffer->reserve(MAX_UDP_BUFFER);
        if (pBuf == nullptr)
        {
            // out of memory
            spdlog::trace("[UdpForwardService::northRead] out of memory");
            return;
        }

        sockaddr_in addr;
        socklen_t addrLen = sizeof(sockaddr_in);
        int nRet = recvfrom(pe->soc, pBuf, MAX_UDP_BUFFER, 0, (sockaddr *)&addr, &addrLen);
        if (nRet > 0)
        {
            // 判断数据包来源是否合法
            if (Utils::compareAddr(&addr, &pe->sockAddr))
            {
                // drop unknown incoming packet
                spdlog::trace("[UdpForwardService::northRead] drop unknown incoming packet");
                continue;
            }

            auto pBlk = mpDynamicBuffer->cut(nRet);
            pBlk->sockaddr = addr;
            pBlk->sockaddr_len = addrLen;

            Endpoint::appendToSendList(&mServiceEndpoint, pBlk);
            // 尝试发送
            southWrite(&mServiceEndpoint);
        }
        else if (nRet < 0)
        {
            if (errno == EAGAIN)
            {
                // 此次数据接收已完毕
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

void UdpForwardService::northWrite(link::Endpoint_t *pe)
{
    auto p = static_cast<buffer::DynamicBuffer::BufBlk_t *>(pe->sendListHead);
    while (p)
    {
        int nRet = send(pe->soc, p->buffer, p->size, 0);
        if (nRet < 0)
        {
            if (errno == EAGAIN)
            {
                // 此次发送窗口已关闭
                break;
            }

            spdlog::debug("[UdpForwardService::northWrite] soc[{}] send fail: {} - [{}]",
                          pe->soc, errno, strerror(errno));

            pe->valid = false;

            // clean send buffer
            while (p)
            {
                mpDynamicBuffer->release(p);
                p = p->next;
            }

            break;
        }

        mpDynamicBuffer->release(p);
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

bool UdpForwardService::onTunnelData(Endpoint_t *pe, UdpTunnel_t *pt, buffer::DynamicBuffer::BufBlk_t *pBufBlk)
{
}

} // namespace link
} // namespace mapper
