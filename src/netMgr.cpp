#include "netMgr.h"

#include <assert.h>
#include <errno.h>
#include <execinfo.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <chrono>
#include <exception>
#include <set>
#include <spdlog/spdlog.h>
#include "link/endpoint.h"
#include "link/tunnel.h"
#include "link/type.h"
#include "link/tcpForwardService.h"
#include "link/udpForwardService.h"
#include "link/utils.h"

using namespace std;

namespace mapper
{

const int NetMgr::INTERVAL_EPOLL_RETRY = 1000;
const int NetMgr::INTERVAL_CONNECT_RETRY = 7;

NetMgr::NetMgr()
    : mpCfg(nullptr),
      mEpollfd(0),
      mStopFlag(true)
{
}

NetMgr::~NetMgr()
{
}

bool NetMgr::start(config::Config &cfg)
{
    spdlog::debug("[NetMgr::start] start.");

    // read settings
    mpCfg = &cfg;
    mForwards = move(mpCfg->getForwards("mapping"));
    mTimer.setInterval(timer::Container::Type_t::TIMER_CONNECT, mpCfg->getGlobalConnectTimeout());
    mTimer.setInterval(timer::Container::Type_t::TIMER_ESTABLISHED, mpCfg->getGlobalSessionTimeout());
    mTimer.setInterval(timer::Container::Type_t::TIMER_BROKEN, mpCfg->getGlobalReleaseTimeout());

    // start thread
    {
        spdlog::debug("[NetMgr::start] start thread");
        if (!mStopFlag)
        {
            spdlog::error("[NetMgr::start] stop thread first");
            return false;
        }

        mStopFlag = false;
        mMainRoutineThread = thread(&NetMgr::threadFunc, this);
    }

    return true;
}

void NetMgr::stop()
{
    // stop threads
    {
        spdlog::debug("[NetMgr::stop] stop threads");
        if (!mStopFlag)
        {
            mStopFlag = true;
        }
        else
        {
            spdlog::debug("[NetMgr::stop] threads not running");
        }
    }

    spdlog::debug("[NetMgr::stop] stop");
}

void NetMgr::join()
{
    if (mMainRoutineThread.joinable())
    {
        spdlog::debug("[NetMgr::join] join main routine thead.");
        mMainRoutineThread.join();
        spdlog::debug("[NetMgr::join] main routine thead stop.");
    }

    spdlog::debug("[NetMgr::join] main routine thread stopped.");
}

void NetMgr::threadFunc()
{
    spdlog::debug("[NetMgr::threadFunc] NetMgr thread start");

    while (!mStopFlag)
    {
        // init env
        spdlog::debug("[NetMgr::threadFunc] init env");
        if (!initEnv())
        {
            spdlog::error("[NetMgr::threadFunc] init fail. wait {} seconds",
                          INTERVAL_CONNECT_RETRY);
            closeEnv();
            this_thread::sleep_for(chrono::seconds(INTERVAL_CONNECT_RETRY));
            continue;
        }

        // main routine
        try
        {
            time_t curTime;
            struct epoll_event event;
            struct epoll_event events[EPOLL_MAX_EVENTS];

            while (!mStopFlag)
            {
                int nRet = epoll_wait(mEpollfd, events, EPOLL_MAX_EVENTS, INTERVAL_EPOLL_RETRY);
                curTime = time(nullptr);
                if (nRet < 0)
                {
                    if (errno == EAGAIN || errno == EINTR)
                    {
                        this_thread::sleep_for(chrono::milliseconds(INTERVAL_EPOLL_RETRY));
                    }
                    else
                    {
                        spdlog::error("[NetMgr::threadFunc] epoll fail. {} - {}",
                                      errno, strerror(errno));
                        break;
                    }
                }
                else if (nRet == 0)
                {
                    // timeout
                }
                else
                {
                    for (int i = 0; i < nRet; ++i)
                    {
                        onSoc(curTime, events[i]);
                    }
                }

                // post-process
                postProcess(curTime);
            }
        }
        catch (const exception &e)
        {
            static const uint32_t BACKTRACE_BUFFER_SIZE = 128;
            void *buffer[BACKTRACE_BUFFER_SIZE];
            char **strings;

            size_t addrNum = backtrace(buffer, BACKTRACE_BUFFER_SIZE);
            strings = backtrace_symbols(buffer, addrNum);

            spdlog::error("[NetMgr::threadFunc] catch an exception. {}", e.what());
            if (strings == nullptr)
            {
                spdlog::error("[NetMgr::threadFunc] backtrace_symbols fail.");
            }
            else
            {
                for (int i = 0; i < addrNum; i++)
                    spdlog::error("[NetMgr::threadFunc]     {}", strings[i]);
                free(strings);
            }
        }

        // close env
        closeEnv();

        if (!mStopFlag)
        {
            spdlog::debug("[NetMgr::threadFunc] sleep {} secnds and try again", INTERVAL_CONNECT_RETRY);
            this_thread::sleep_for(chrono::seconds(INTERVAL_CONNECT_RETRY));
        }
    }

    spdlog::debug("[NetMgr::threadFunc] NetMgr thread stop");
}

bool NetMgr::initEnv()
{
    // create epoll
    spdlog::debug("[NetMgr::initEnv] create epoll");
    if ((mEpollfd = epoll_create1(0)) < 0)
    {
        spdlog::error("[NetMgr::initEnv] Failed to create epoll. {} - {}",
                      errno, strerror(errno));
        return false;
    }

    int index = 0;
    for (auto forward : mForwards)
    {
        spdlog::debug("[NetMgr::initEnv] process forward: {}", forward->toStr());

        link::Protocol_t protocol = link::Utils::parseProtocol(forward->protocol);

        if (protocol == link::Protocol_t::TCP)
        {
            // link::EndpointService_t *pes = link::Endpoint::createService(forward->protocol.c_str(),
            //                                                              forward->interface.c_str(),
            //                                                              forward->service.c_str(),
            //                                                              forward->targetHost.c_str(),
            //                                                              forward->targetService.c_str());
            // if (pes == nullptr)
            // {
            //     spdlog::error("[NetMgr::initEnv] create service endpoint fail");
            //     return false;
            // }

            // // add service endpoint into epoll driver
            // if (!epollAddEndpoint(pes, true, false, false))
            // {
            //     spdlog::error("[NetMgr::initEnv] add service endpoint[{}] into epoll fail.", forward->toStr());
            //     link::Endpoint::releaseService(pes);
            //     return false;
            // }

            // mServices.push_back(pes);
            // spdlog::info("[NetMgr::initEnv] forward[{}] -- soc[{}] -- {}", index++, pes->soc, forward->toStr());

            link::TcpForwardService *pService = new link::TcpForwardService();
            if (pService == nullptr)
            {
                spdlog::error("[NetMgr::initEnv] create instance of Tcp Forward Service fail");
                return false;
            }
            if (!pService->init(mEpollfd, forward, mpCfg->getLinkSharedBuffer()))
            {
                spdlog::error("[NetMgr::initEnv] init instance of Tcp Forward Service fail");
                delete pService;
                return false;
            }

            mTcpServices.push_back(pService);
            spdlog::info("[NetMgr::initEnv] forward[{}] -- soc[{}] -- {}",
                         index++,
                         pService->getServiceEndpoint().soc,
                         forward->toStr());
        }
        else
        {
            link::UdpForwardService *pService = new link::UdpForwardService();
            if (pService == nullptr)
            {
                spdlog::error("[NetMgr::initEnv] create instance of Udp Forward Service fail");
                return false;
            }
            if (!pService->init(mEpollfd, forward, mpCfg->getLinkUdpBuffer()))
            {
                spdlog::error("[NetMgr::initEnv] init instance of Udp Forward Service fail");
                delete pService;
                return false;
            }

            mUdpServices.push_back(pService);
            spdlog::info("[NetMgr::initEnv] forward[{}] -- soc[{}] -- {}",
                         index++,
                         pService->getServiceEndpoint().soc,
                         forward->toStr());
        }
    }

    // // init tunnel manager
    // if (!mTunnelMgr.init(mpCfg))
    // {
    //     spdlog::error("[NetMgr::initEnv] init tunnel manager fail");
    //     return false;
    // }

    return true;
}

void NetMgr::closeEnv()
{
    // close service endpoint
    spdlog::debug("[NetMgr::closeEnv] close service endpoint");
    for (auto pes : mServices)
    {
        // 如果为 UDP Service 则还需释放对应资源
        if (pes->protocol == link::Protocol_t::UDP && pes->udpService)
        {
            spdlog::debug("[NetMgr::closeEnv] release resources for UDP service endpoint[{}]",
                          link::Endpoint::toStr(pes));
            // release UDP tunnel manager
            pes->udpService->close();
            delete pes->udpService;
            pes->udpService = nullptr;
        }

        epollRemoveEndpoint(pes);
        link::Endpoint::releaseService(pes);
    }
    mServices.clear();

    if (!mTcpServices.empty())
    {
        for (auto ps : mTcpServices)
        {
            ps->close();
            delete ps;
        }
        mTcpServices.clear();
    }
    if (!mUdpServices.empty())
    {
        for (auto ps : mUdpServices)
        {
            ps->close();
            delete ps;
        }
        mUdpServices.clear();
    }

    // close epoll file descriptor
    spdlog::debug("[NetMgr::closeEnv] close epoll file descriptor");
    if (mEpollfd)
    {
        if (close(mEpollfd))
        {
            spdlog::error("[NetMgr::closeEnv] Fail to close file descriptor[{}]. {} - {}",
                          mEpollfd, errno, strerror(errno));
        }
        mEpollfd = 0;
    }

    // // close tunnel manager
    // spdlog::debug("[NetMgr::closeEnv] close tunnel manager");
    // mTunnelMgr.close();
}

void NetMgr::onSoc(time_t curTime, epoll_event &event)
{
    link::Endpoint_t *pe = (link::Endpoint_t *)event.data.ptr;
    link::Service *pService = (link::Service *)pe->service;
    pService->onSoc(curTime, event.events, pe);

    // link::EndpointBase_t *peb = (link::EndpointBase_t *)event.data.ptr;
    // // spdlog::trace("[NetMgr::onSoc] {}", pe->toStr());

    // if (peb->protocol == link::Protocol_t::TCP)
    // {
    //     // TCP
    //     if (event.events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR))
    //     {
    //         // connection broken
    //         stringstream ss;
    //         if (event.events & EPOLLRDHUP)
    //         {
    //             ss << "closed by peer;";
    //         }
    //         if (event.events & EPOLLHUP)
    //         {
    //             ss << "hang up;";
    //         }
    //         if (event.events & EPOLLERR)
    //         {
    //             ss << "error;";
    //         }

    //         peb->valid = false;
    //         if (peb->type == link::Type_t::SERVICE)
    //         {
    //             link::EndpointService_t *pes = (link::EndpointService_t *)peb;
    //             spdlog::error("[NetMgr::onSoc] service endpoint[{}] broken",
    //                           link::Endpoint::toStr(pes));
    //             ::close(pes->soc);
    //             pes->soc = 0;
    //         }
    //         else
    //         {
    //             spdlog::trace("[NetMgr::onSoc] endpoint[{}] broken: {}", link::Endpoint::toStr(peb), ss.str());
    //             link::EndpointRemote_t *per = (link::EndpointRemote_t *)peb;
    //             link::Tunnel_t *pt = (link::Tunnel_t *)per->tunnel;
    //             mPostProcessList.insert(pt);
    //         }

    //         return;
    //     }

    //     if (peb->type == link::Type_t::SERVICE)
    //     {
    //         if (event.events & EPOLLIN)
    //         {
    //             // accept client for TCP service endpoint
    //             auto pes = (link::EndpointService_t *)peb;
    //             acceptClient(curTime, pes);
    //         }
    //     }
    //     else
    //     {
    //         bool retSend = true;
    //         bool retRecv = true;
    //         auto per = (link::EndpointRemote_t *)peb;

    //         auto pt = (link::Tunnel_t *)per->tunnel;

    //         // TCP - send
    //         if (event.events & EPOLLOUT)
    //         {
    //             retSend = onSend(curTime, per, pt);
    //         }

    //         // TCP - recv
    //         if (event.events & EPOLLIN)
    //         {
    //             retRecv = onRecv(curTime, per, pt);
    //         }

    //         if (retSend && retRecv)
    //         {
    //             // 收发成功，更新时间戳
    //             mTimer.refresh(curTime, &pt->timerClient);
    //         }
    //         else
    //         {
    //             mPostProcessList.insert(pt);
    //         }
    //     }
    // }
    // else
    // {
    //     // UDP
    //     link::Endpoint_t *pe = (link::Endpoint_t *)event.data.ptr;
    //     link::Service *pService = (link::Service *)pe->service;
    //     pService->onSoc(curTime, event.events, pe);
    //     // if (peb->type == link::Type_t::SERVICE)
    //     // {
    //     //     auto pes = (link::EndpointService_t *)peb;
    //     //     pes->udpService->onSouthSoc(curTime, event.events, pes);
    //     // }
    //     // else
    //     // {
    //     //     // TODO: ...
    //     //     // link::EndpointRemote_t *per = (link::EndpointRemote_t *)peb;
    //     //     // link::UdpTunnel_t *put = (link::UdpTunnel_t *)per->tunnel;
    //     //     // link::EndpointService_t *pes = (link::EndpointService_t *)put->service;
    //     //     // pes->udpService->onNorthSoc(curTime, event.events, per);
    //     // }
    // }
}

void NetMgr::acceptClient(time_t curTime, link::EndpointService_t *pes)
{
    // accept client
    struct sockaddr_in address;
    int addrlen = sizeof(address);

    int southSoc = accept(pes->soc, (struct sockaddr *)&address, (socklen_t *)&addrlen);
    if (southSoc == -1)
    {
        spdlog::error("[NetMgr::acceptClient] accept fail: {} - {}", errno, strerror(errno));
        return;
    }
    // set client socket to non-block mode
    if (fcntl(southSoc, F_SETFL, O_NONBLOCK) < 0)
    {
        spdlog::error("[NetMgr::acceptClient] set socket to non-blocking mode fail. {}: {}", errno, strerror(errno));
        ::close(southSoc);
        return;
    }

    // alloc tunnel
    auto pt = mTunnelMgr.allocTunnel();
    if (pt == nullptr)
    {
        spdlog::error("[NetMgr::acceptClient] alloc tunnel fail");
        close(southSoc);
        return;
    }
    if (!link::Tunnel::init(pt, pes, southSoc))
    {
        spdlog::error("[NetMgr::acceptClient] init tunnel fail");
        close(southSoc);
        mTunnelMgr.freeTunnel(pt);
        return;
    }

    // connect to target
    if (!link::Tunnel::connect(pt))
    {
        spdlog::error("[NetMgr::acceptClient] connect to target fail");
        close(southSoc);
        mTunnelMgr.freeTunnel(pt);
        return;
    }

    // add tunnel into epoll driver
    if (!epollAddTunnel(pt))
    {
        spdlog::error("[NetMgr::acceptClient] add tunnel into epoll driver fail");
        pt->close();
        mTunnelMgr.freeTunnel(pt);
        return;
    }

    // add into timeout timer
    mTimer.insert(timer::Container::Type_t::TIMER_CONNECT, curTime, &pt->timerClient);

    char ip[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &address.sin_addr, ip, INET_ADDRSTRLEN);
    spdlog::debug("[NetMgr::acceptClient] accept client[{},{}]({}:{})--{}-->{}:{}",
                  pt->south.soc, pt->north.soc,
                  ip, ntohs(address.sin_port),
                  pes->protocol == link::Protocol_t::TCP ? "tcp" : "udp",
                  pes->targetHost, pes->targetService);
}

bool NetMgr::onSend(time_t curTime, link::EndpointRemote_t *per, link::Tunnel_t *pt)
{
    if (per->type == link::Type_t::NORTH)
    {
        if (!pt->north.valid)
        {
            spdlog::error("[NetMgr::onSend] can't send on invalid north soc[{}]",
                          pt->north.soc);
            return false;
        }

        // 状态处理
        switch (pt->status)
        {
        case link::TunnelState_t::CONNECT:
            // 会话已建立，并且此时并没有数据需要发送
            if (epollResetEndpointMode(&pt->north, true, true, true) &&
                epollResetEndpointMode(&pt->south, true, true, true))
            {
                link::Tunnel::setStatus(pt, link::TunnelState_t::ESTABLISHED);

                spdlog::debug("[NetMgr::onSend] tunnel[{},{}] established.",
                              pt->south.soc, pt->north.soc);
                // remove from connect timer container
                mTimer.remove(&pt->timerClient);
                // insert into established timer container
                mTimer.insert(timer::Container::Type_t::TIMER_ESTABLISHED,
                              curTime,
                              &pt->timerClient);
                return true;
            }
            else
            {
                spdlog::error("[NetMgr::onSend] north soc[{}] connect fail",
                              pt->north.soc);
                return false;
            }
        case link::TunnelState_t::ESTABLISHED:
        case link::TunnelState_t::BROKEN:
            break;
        default:
            spdlog::error("[NetMgr::onSend] north soc[{}] invalid tunnel status: {}",
                          pt->north.soc, pt->status);
            assert(false);
        }

        if (!link::Tunnel::northSocSend(pt))
        {
            // spdlog::error("[NetMgr::onSend] north soc[{}] send fail", pt->north.soc);
            return false;
        }
        else
        {
            // 判断是否已有能力接收从南向来的数据
            if (pt->status == link::TunnelState_t::ESTABLISHED && // 只在链路建立的状态下接收来自南向的数据
                pt->toNorthBuffer->stopRecv &&                    // 北向缓冲区之前因无空间而停止接收数据
                pt->toNorthBuffer->freeSize() &&                  // 北向缓冲区当前可以接收数据
                pt->south.valid)                                  // 南向链路有效
            {
                pt->toNorthBuffer->stopRecv = false;
                if (!epollResetEndpointMode(&pt->south, true, true, true))
                {
                    spdlog::error("[NetMgr::onSend] reset south soc[{}] fail", pt->south.soc);
                    return false;
                }
            }

            return true;
        }
    }
    else
    {
        if (!pt->south.valid)
        {
            spdlog::error("[NetMgr::onSend] can't send on invalid south soc[{}]",
                          pt->south.soc);
            return false;
        }

        // 状态处理
        switch (pt->status)
        {
        case link::TunnelState_t::ESTABLISHED:
        case link::TunnelState_t::BROKEN:
            break;
        default:
            spdlog::error("[NetMgr::onSend] south soc[{}] invalid tunnel status: {}",
                          pt->south.soc, pt->status);
            assert(false);
        }

        if (!link::Tunnel::southSocSend(pt))
        {
            // spdlog::error("[NetMgr::onSend] south soc[{}] send fail", pt->south.soc);
            return false;
        }
        else
        {
            // 判断是否已有能力接收从北向来的数据
            if (pt->status == link::TunnelState_t::ESTABLISHED && // 只在链路建立的状态下接收来自南向的数据
                pt->toSouthBuffer->stopRecv &&                    // 南向缓冲区之前因无空间而停止接收数据
                pt->toSouthBuffer->freeSize() &&                  // 南向缓冲区当前可以接收数据
                pt->north.valid)                                  // 北向链路有效
            {
                pt->toSouthBuffer->stopRecv = false;
                if (!epollResetEndpointMode(&pt->north, true, true, true))
                {
                    spdlog::error("[NetMgr::onSend] reset north soc[{}] fail", pt->north.soc);
                    return false;
                }
            }

            return true;
        }
    }
}

bool NetMgr::onRecv(time_t curTime, link::EndpointRemote_t *per, link::Tunnel_t *pt)
{
    if (per->type == link::Type_t::NORTH)
    {
        if (!pt->north.valid)
        {
            spdlog::error("[NetMgr::onRecv] can't recv on invalid north soc[{}]",
                          pt->north.soc);
            return false;
        }

        // 状态处理
        switch (pt->status)
        {
        case link::TunnelState_t::ESTABLISHED:
            break;
        case link::TunnelState_t::BROKEN:
            // 此状态下，不接收新数据
            return true;
        default:
            spdlog::error("[NetMgr::onRecv] north soc[{}] invalid tunnel status: {}",
                          pt->north.soc, pt->status);
            assert(false);
        }

        if (link::Tunnel::northSocRecv(pt))
        {
            // 收到数据后立即尝试发送
            if (pt->toSouthBuffer->dataSize() && !link::Tunnel::southSocSend(pt))
            {
                // spdlog::error("[NetMgr::onRecv] send data to south fail");
                return false;
            }
            if (pt->toSouthBuffer->stopRecv && pt->toSouthBuffer->freeSize())
            {
                if (epollResetEndpointMode(&pt->north, true, true, true))
                {
                    pt->toSouthBuffer->stopRecv = false;
                }
                else
                {
                    spdlog::error("[NetMgr::onRecv] reset north soc[{}] fail", pt->north.soc);
                    return false;
                }
            }

            return true;
        }
        else
        {
            spdlog::error("[NetMgr::onRecv] north soc[{}] recv fail", pt->north.soc);
            return false;
        }
    }
    else
    {
        if (!pt->south.valid)
        {
            spdlog::error("[NetMgr::onRecv] can't recv on invalid south soc[{}]",
                          pt->south.soc);
            return false;
        }

        // 状态处理
        switch (pt->status)
        {
        case link::TunnelState_t::ESTABLISHED:
            break;
        case link::TunnelState_t::BROKEN:
            // 此状态下，不接收新数据
            return true;
        default:
            spdlog::error("[NetMgr::onRecv] south soc[{}] invalid tunnel status: {}",
                          pt->south.soc, pt->status);
            assert(false);
        }

        if (link::Tunnel::southSocRecv(pt))
        {
            // 收到数据后立即尝试发送
            if (pt->toNorthBuffer->dataSize() && !link::Tunnel::northSocSend(pt))
            {
                // spdlog::error("[NetMgr::onRecv] send data to north fail");
                return false;
            }
            if (pt->toNorthBuffer->stopRecv && pt->toNorthBuffer->freeSize())
            {
                if (epollResetEndpointMode(&pt->south, true, true, true))
                {
                    pt->toNorthBuffer->stopRecv = false;
                }
                else
                {
                    spdlog::error("[NetMgr::onRecv] reset north soc[{}] fail", pt->north.soc);
                    return false;
                }
            }

            return true;
        }
        else
        {
            spdlog::error("[NetMgr::onRecv] south soc[{}] recv fail", pt->south.soc);
            return false;
        }
    }
}

bool NetMgr::epollAddTunnel(link::Tunnel_t *pt)
{
    if (!epollAddEndpoint(&pt->south, false, false, true) ||
        !epollAddEndpoint(&pt->north, false, true, true))
    {
        spdlog::error("[NetMgr::epollAddTunnel] add tunnel({}) fail", link::Endpoint::toStr(pt));
        return false;
    }

    return true;
}

void NetMgr::epollRemoveTunnel(link::Tunnel_t *pt)
{
    epollRemoveEndpoint(&pt->north);
    epollRemoveEndpoint(&pt->south);
}

bool NetMgr::epollAddEndpoint(link::EndpointBase_t *pe, bool read, bool write, bool edgeTriger)
{
    // spdlog::debug("[NetMgr::epollAddEndpoint] endpoint[{}], read[{}], write[{}]",
    //               link::Endpoint::toStr(pe), read, write);

    struct epoll_event event;
    event.data.ptr = pe;
    event.events = EPOLLRDHUP |                // for peer close
                   (read ? EPOLLIN : 0) |      // enable read
                   (write ? EPOLLOUT : 0) |    // enable write
                   (edgeTriger ? EPOLLET : 0); // use edge triger or level triger
    if (epoll_ctl(mEpollfd, EPOLL_CTL_ADD, pe->soc, &event))
    {
        spdlog::error("[NetMgr::epollAddEndpoint] events[{}]-soc[{}] join fail. Error {}: {}",
                      event.events, pe->soc, errno, strerror(errno));
        return false;
    }

    // spdlog::debug("[NetMgr::epollAddEndpoint] endpoint[{}], event.events[0x{:X}]",
    //               link::Endpoint::toStr(pe), event.events);

    return true;
}

void NetMgr::epollRemoveEndpoint(link::EndpointBase_t *pe)
{
    if (pe)
    {
        // spdlog::trace("[NetMgr::epollRemoveEndpoint] remove endpoint[{}]",
        //               link::Endpoint::toStr(pe));

        // remove from epoll driver
        if (epoll_ctl(mEpollfd, EPOLL_CTL_DEL, pe->soc, nullptr))
        {
            spdlog::error("[NetMgr::removeAndCloseSoc] remove endpoint[{}] from epoll fail. {} - {}",
                          link::Endpoint::toStr(pe), errno, strerror(errno));
        }
        // close socket
        if (close(pe->soc))
        {
            spdlog::error("[NetMgr::removeAndCloseSoc] Close endpoint[{}] fail. {} - {}",
                          link::Endpoint::toStr(pe), errno, strerror(errno));
        }
        pe->soc = 0;
    }
}

bool NetMgr::epollResetEndpointMode(link::EndpointBase_t *pe, bool read, bool write, bool edgeTriger)
{
    // spdlog::debug("[NetMgr::epollResetEndpointMode] endpoint[{}], read: {}, write: {}, edgeTriger: {}",
    //               link::Endpoint::toStr(pe), read, write, edgeTriger);

    struct epoll_event event;
    event.data.ptr = pe;
    event.events = EPOLLRDHUP |                // for peer close
                   (read ? EPOLLIN : 0) |      // enable read
                   (write ? EPOLLOUT : 0) |    // enable write
                   (edgeTriger ? EPOLLET : 0); // use edge triger or level triger
    if (epoll_ctl(mEpollfd, EPOLL_CTL_MOD, pe->soc, &event))
    {
        spdlog::error("[NetMgr::epollResetEndpointMode] events[{}]-soc[{}] reset fail. Error {}: {}",
                      event.events, pe->soc, errno, strerror(errno));
        return false;
    }

    return true;
}

void NetMgr::postProcess(time_t curTime)
{
    if (!mPostProcessList.empty())
    {
        // 进行会话状态处理
        for (auto pt : mPostProcessList)
        {
            switch (pt->status)
            {
            case link::TunnelState_t::CONNECT:
                if (pt->south.valid)
                {
                    // 只有当南向链路完好时才尝试进行北向重连操作
                    if (link::Tunnel::connect(pt))
                    {
                        // refresh timeout timer
                        mTimer.refresh(curTime, &pt->timerClient);
                    }
                    else
                    {
                        // reconnect failed

                        // remove out of timeout container
                        mTimer.remove(&pt->timerClient);

                        // spdlog::trace("[NetMgr::postProcess] tunnel({}) reconnect failed",
                        //               link::Endpoint::toStr(pt));
                        link::Tunnel::setStatus(pt, link::TunnelState_t::BROKEN);
                        onClose(pt);
                    }
                }
                else
                {
                    // set tunnel status to broken
                    link::Tunnel::setStatus(pt, link::TunnelState_t::BROKEN);
                    // close tunnel
                    onClose(pt);
                }
                break;
            case link::TunnelState_t::ESTABLISHED:
                // remove from 'established' timeout container
                mTimer.remove(&pt->timerClient);
                // set tunnel status to broken
                link::Tunnel::setStatus(pt, link::TunnelState_t::BROKEN);
                // insert into 'broken' timeout container
                mTimer.insert(timer::Container::Type_t::TIMER_BROKEN,
                              curTime,
                              &pt->timerClient);
                // close tunnel
                onClose(pt);
                break;
            case link::TunnelState_t::BROKEN:
                // remove from 'broken' timeout container
                mTimer.remove(&pt->timerClient);
                // close tunnel
                pt->north.valid = false;
                pt->south.valid = false;
                onClose(pt);
                break;
            default:
                spdlog::critical("[NetMgr::postProcess] invalid tunnel status: {}", pt->status);
                assert(false);
            }
        }
        mPostProcessList.clear();
    }

    // timeout check
    timeoutCheck(curTime);
}

void NetMgr::onClose(link::Tunnel_t *pt)
{
    if (pt->status != link::TunnelState_t::BROKEN)
    {
        spdlog::critical("[NetMgr::onClose] invalid tunnel status: {}", pt->status);
        assert(false);
    }

    if ((pt->toNorthBuffer->empty() || !pt->north.valid) &&
        (pt->toSouthBuffer->empty() || !pt->south.valid))
    {
        // release session object
        spdlog::debug("[NetMgr::onClose] close tunnel[{},{}]", pt->south.soc, pt->north.soc);
        epollRemoveTunnel(pt);

        // remove from timer container
        if (pt->timerClient.inTimer)
        {
            mTimer.remove(&pt->timerClient);
        }
        else
        {
            assert(!pt->timerClient.inTimer &&
                   pt->timerClient.type == timer::Container::Type_t::TYPE_INVALID);
        }

        // release session object
        mTunnelMgr.freeTunnel(pt);
    }
    else if (!pt->toNorthBuffer->empty() && pt->north.valid)
    {
        // send last data to north
        if (!epollResetEndpointMode(&pt->north, false, true, true))
        {
            spdlog::error("[NetMgr::onClose] Failed to modify north sock[{}] in epoll for last data. {} - {}",
                          pt->north.soc, errno, strerror(errno));
            pt->north.valid = false;
            onClose(pt);
        }
    }
    else
    {
        assert(!pt->toSouthBuffer->empty() && pt->south.valid);

        // send last data to south
        if (!epollResetEndpointMode(&pt->south, false, true, true))
        {
            spdlog::error("[NetMgr::onClose] Failed to modify south sock[{}] in epoll for last data. {} - {}",
                          pt->south.soc, errno, strerror(errno));
            pt->south.valid = false;
            onClose(pt);
        }
    }
}

void NetMgr::timeoutCheck(time_t curTime)
{
    auto fn = [&](time_t curTime,
                  timer::Container::Type_t type,
                  const char *title) {
        for (auto p = mTimer.removeTimeout(type, curTime); p; p = p->next)
        {
            auto pt = (link::Tunnel_t *)p->tag;
            spdlog::debug("[NetMgr::timeoutCheck] tunnel[{}]@{} timeout.", link::Tunnel::toStr(pt), title);
            link::Tunnel::setStatus(pt, link::TunnelState_t::BROKEN);
            onClose(pt);
        }
    };

    for (int type = 0; type < timer::Container::Type_t::TYPE_COUNT; ++type)
    {
        auto p = mTimer.removeTimeout((timer::Container::Type_t)type, curTime);
        for (; p; p = p->next)
        {
            auto pt = (link::Tunnel_t *)p->tag;
            spdlog::debug("[NetMgr::timeoutCheck] tunnel[{}]@{} timeout.", link::Tunnel::toStr(pt), type);
            link::Tunnel::setStatus(pt, link::TunnelState_t::BROKEN);
            onClose(pt);
        }
    }
}

} // namespace mapper
