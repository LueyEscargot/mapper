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
#include "session.h"
#include "link/endpoint.h"
#include "link/tunnel.h"
#include "link/type.h"

using namespace std;

namespace mapper
{

const int NetMgr::INTERVAL_EPOLL_RETRY = 100;
const int NetMgr::INTERVAL_CONNECT_RETRY = 7;

NetMgr::NetMgr()
    : mpCfg(nullptr),
      mPreConnEpollfd(0),
      mEpollfd(0),
      mStopFlag(true),
      mConnectTimeout(0),
      mSessionTimeout(0)
{
}

NetMgr::~NetMgr()
{
    stop();
}

bool NetMgr::start(config::Config &cfg)
{
    spdlog::debug("[NetMgr::start] start.");

    mpCfg = &cfg;

    mForwards = move(mpCfg->getMapData());
    mConnectTimeout = mpCfg->getAsUint32("connectionTimeout", "global", CONNECT_TIMEOUT);
    mSessionTimeout = mpCfg->getAsUint32("sessionTimeout", "global", SESSION_TIMEOUT);

    // start thread
    {
        spdlog::debug("[NetMgr::start] start thread");
        if (!mStopFlag)
        {
            spdlog::error("[NetMgr::start] stop thread first");
            return false;
        }

        mStopFlag = false;
        mThreads.emplace_back(&NetMgr::threadFunc, this);
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
            join();
            mThreads.clear();
        }
        else
        {
            spdlog::debug("[NetMgr::stop] threads not running");
        }
    }
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
    spdlog::debug("[NetMgr::initEnv] create epolls");
    if ((mPreConnEpollfd = epoll_create1(0)) < 0)
    {
        spdlog::error("[NetMgr::initEnv] Failed to create pre-conn epoll. {} - {}",
                      errno, strerror(errno));
        return false;
    }
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

        link::EndpointService_t *pes = link::Endpoint::createService(forward->protocol.c_str(),
                                                                     forward->interface.c_str(),
                                                                     forward->service.c_str(),
                                                                     forward->targetHost.c_str(),
                                                                     forward->targetService.c_str());
        if (pes == nullptr)
        {
            spdlog::error("[NetMgr::initEnv] create service endpoint fail");
            return false;
        }

        // add service endpoint into epoll driver
        if (!epollAddEndpoint(pes, true, false, false))
        {
            spdlog::error("[NetMgr::initEnv] add service endpoint[{}] into epoll fail.");
            link::Endpoint::releaseService(pes);
            return false;
        }

        mServices.push_back(pes);

        spdlog::info("[NetMgr::initEnv] forward[{}] -- soc[{}] -- {}", index++, pes->soc, forward->toStr());
    }

    // init tunnel manager
    if (!mTunnelMgr.init(mpCfg))
    {
        spdlog::error("[NetMgr::initEnv] init tunnel manager fail");
        return false;
    }

    return true;
}

void NetMgr::closeEnv()
{
    // close service endpoint
    spdlog::debug("[NetMgr::closeEnv] close service endpoint");
    for (auto pes : mServices)
    {
        epollRemoveEndpoint(pes);
        link::Endpoint::releaseService(pes);
    }
    mServices.clear();

    // close epoll file descriptors
    auto f = [](int &fd) {
        if (close(fd))
        {
            spdlog::error("[NetMgr::closeEnv] Fail to close file descriptor[{}]. {} - {}",
                          fd, errno, strerror(errno));
        }
        fd = 0;
    };
    spdlog::debug("[NetMgr::closeEnv] close file descriptors");
    f(mPreConnEpollfd);
    f(mEpollfd);
    mPreConnEpollfd = 0;
    mEpollfd = 0;

    // close tunnel manager
    spdlog::debug("[NetMgr::closeEnv] close tunnel manager");
    mTunnelMgr.close();
}

void NetMgr::onSoc(time_t curTime, epoll_event &event)
{
    link::EndpointBase_t *pEndpoint = static_cast<link::EndpointBase_t *>(event.data.ptr);
    // spdlog::trace("[NetMgr::onSoc] {}", pEndpoint->toStr());

    if (event.events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR))
    {
        // connection broken
        stringstream ss;
        if (event.events & EPOLLRDHUP)
        {
            ss << "closed by peer;";
        }
        if (event.events & EPOLLHUP)
        {
            ss << "hang up;";
        }
        if (event.events & EPOLLERR)
        {
            ss << "error;";
        }

        pEndpoint->valid = false;
        if (pEndpoint->type & (Endpoint::Type_t::NORTH | Endpoint::Type_t::SOUTH))
        {
            spdlog::trace("[NetMgr::onSoc] endpoint[{}] broken: {}", link::Endpoint::toStr(pEndpoint), ss.str());
            link::EndpointRemote_t *per = static_cast<link::EndpointRemote_t *>(pEndpoint);
            link::Tunnel_t *pt = static_cast<link::Tunnel_t *>(per->tunnel);
            mPostProcessList.insert(pt);
        }
        else
        {
            spdlog::error("[NetMgr::onSoc] service endpoint[{}] broken",
                          link::Endpoint::toStr(static_cast<link::EndpointService_t *>(pEndpoint)));
            link::EndpointService_t *pes = static_cast<link::EndpointService_t *>(pEndpoint);
        }

        return;
    }

    switch (pEndpoint->type)
    {
    case Endpoint::Type_t::SERVICE:
        onService(curTime, event.events, static_cast<link::EndpointService_t *>(pEndpoint));
        break;
    case Endpoint::Type_t::NORTH:
    case Endpoint::Type_t::SOUTH:
    {
        link::EndpointRemote_t *per = static_cast<link::EndpointRemote_t *>(pEndpoint);

        // spdlog::trace("[NetMgr::onSoc] Session: {}", link::Endpoint::toStr(per));

        using namespace std::placeholders;
        if (!link::Tunnel::onSoc(curTime,
                                 per,
                                 event.events,
                                 [&](link::EndpointBase_t *pe,
                                     bool read,
                                     bool write,
                                     bool edgeTriger) -> bool {
                                     return epollResetEndpointMode(pe, read, write, edgeTriger);
                                 }))
        {
            spdlog::error("[NetMgr::onSoc] endpoint[{}] process fail", per->soc);
            link::Tunnel_t *pt = static_cast<link::Tunnel_t *>(per->tunnel);
            mPostProcessList.insert(pt);
        }
    }
    break;
    default:
        spdlog::critical("[NetMgr::onSoc] invalid endpoint:", link::Endpoint::toStr(pEndpoint));
        assert(false);
    }
}

void NetMgr::onService(time_t curTime, uint32_t events, link::EndpointService_t *pEndpoint)
{
    // accept client for TCP service endpoint
    if (events & EPOLLIN)
    {
        acceptClient(curTime, pEndpoint);
    }

    // TODO: UDP service endpoint
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

    // TODO: add into timeout timer
    mConnectTimer.insert(curTime, &pt->timerClient);

    char ip[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &address.sin_addr, ip, INET_ADDRSTRLEN);
    spdlog::debug("[NetMgr::acceptClient] accept client[{}-{}]({}:{})--{}-->{}:{}",
                  pt->south.soc, pt->north.soc,
                  ip, ntohs(address.sin_port),
                  pes->protocol == link::Protocol_t::TCP ? "tcp" : "udp",
                  pes->targetHost, pes->targetService);
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
                        // TODO: refresh timeout timer
                    }
                    else
                    {
                        // reconnect failed
                        // TODO: remove out of timeout timer
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
                // TODO: switch timeout timer

                // set tunnel status to broken
                link::Tunnel::setStatus(pt, link::TunnelState_t::BROKEN);
                // close tunnel
                onClose(pt);
                break;
            case link::TunnelState_t::BROKEN:
                // close tunnel
                // TODO: refresh timeout timer
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

    if ((pt->toNorthBUffer->empty() || !pt->north.valid) &&
        (pt->toSouthBUffer->empty() || !pt->south.valid))
    {
        // release session object
        spdlog::debug("[NetMgr::onClose] close tunnel[{}-{}]", pt->south.soc, pt->north.soc);
        epollRemoveTunnel(pt);

        // release session object
        mTunnelMgr.freeTunnel(pt);
    }
    else if (!pt->toNorthBUffer->empty() && pt->north.valid)
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
        assert(!pt->toSouthBUffer->empty() && pt->south.valid);

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
    auto fn = [&](uint64_t interval,
                  timer::Container &c,
                  const char *title) {
        for (auto p = c.removeTimeout(curTime - interval); p; p = p->next)
        {
            auto pt = static_cast<link::Tunnel_t *>(p->self);
            spdlog::debug("[NetMgr::timeoutCheck] tunnel[{}]@{} timeout.", link::Tunnel::toStr(pt), title);
            link::Tunnel::setStatus(pt, link::TunnelState_t::BROKEN);
            onClose(pt);
        }
    };

    if (!mConnectTimer.empty())
    {
        fn(mConnectTimeout, mConnectTimer, "CONN");
    }
    // if (!mTunnelTimer.empty())
    // {
    //     fn(curTime, mSessionTimeout, mTunnelTimer, "ESTB");
    // }
}

} // namespace mapper
