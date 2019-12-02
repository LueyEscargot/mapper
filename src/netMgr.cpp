#include "netMgr.h"

#include <assert.h>
#include <errno.h>
#include <execinfo.h>
#include <signal.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/epoll.h>
#include <sys/signalfd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <chrono>
#include <exception>
#include <set>
#include <spdlog/spdlog.h>
#include "session.h"
#include "link/endpoint.h"

using namespace std;

namespace mapper
{

const int NetMgr::INTERVAL_EPOLL_RETRY = 100;
const int NetMgr::INTERVAL_CONNECT_RETRY = 7;

NetMgr::NetMgr()
    : mpCfg(nullptr),
      mPreConnEpollfd(0),
      mEpollfd(0),
      mSignalfd(0),
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

        if (!mSessionMgr.init(mpCfg->getBufferSize(BUFFER_SIZE),
                              mpCfg->getSessions(DEFAULT_SESSIONS)))
        {
            spdlog::error("[NetMgr::start] init session manager fail");
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

    // release session manager
    mSessionMgr.release();
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
    // create event file descriptor
    {
        sigset_t sigs;

        sigemptyset(&sigs);
        sigaddset(&sigs, SIGRTMIN);
        sigprocmask(SIG_BLOCK, &sigs, NULL);

        if (mSignalfd = signalfd(-1, &sigs, SFD_NONBLOCK | SFD_CLOEXEC))
        {
            spdlog::error("[NetMgr::initEnv] create event file descriptor fail: {} - {}",
                          errno, strerror(errno));
            return false;
        }
    }

    int index = 0;
    for (auto forward : mForwards)
    {
        spdlog::debug("[NetMgr::initEnv] process forward: {}", forward->toStr());

        // create service endpoint
        link::EndpointService_t *pService = link::Endpoint::createService(forward->toStr());
        if (pService == nullptr)
        {
            spdlog::error("[NetMgr::initEnv] create service endpoint fail");
            return false;
        }

        // TODO: remove related variables & save endpoint object for release reason.
        //
        // // save service endpoint
        // Endpoint *pEndpoint = new Endpoint(Endpoint::Type_t::SERVICE, soc, serviceIndexToPtr(index));
        // if (pEndpoint == nullptr)
        // {
        //     spdlog::error("[NetMgr::initEnv] create endpoint object fail: {} - {}",
        //                   errno, strerror(errno));
        //     return false;
        // }
        // mSvrEndpoints.emplace_back(pEndpoint);

        // add service endpoint into epoll driver
        struct epoll_event event;
        event.events = EPOLLIN | EPOLLRDHUP;
        event.data.ptr = pService;
        if (epoll_ctl(mPreConnEpollfd, EPOLL_CTL_ADD, pService->soc, &event))
        {
            spdlog::error("[NetMgr::initEnv] add service endpoint[{}] into epoll fail. Error {}: {}",
                          pService->soc, errno, strerror(errno));
            link::Endpoint::releaseService(pService);
            return false;
        }

        spdlog::info("[NetMgr::initEnv] forward[{}] -- soc[{}] -- {}", index++, pService->soc, forward->toStr());
    }

    // init DNS request manager
    if (!mDnsReqMgr.init(mpCfg->getLinkMaxDnsReqs()))
    {
        spdlog::error("[NetMgr::initEnv] init DNS request manager fail");
        return false;
    }

    return true;
}

void NetMgr::closeEnv()
{
    // close service endpoint
    spdlog::debug("[NetMgr::closeEnv] close service endpoint");
    for (auto pEndpoint : mSvrEndpoints)
    {
        assert(pEndpoint->check());
        removeAndCloseSoc(pEndpoint->soc);
    }
    mSvrEndpoints.clear();

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
    f(mSignalfd);
    f(mPreConnEpollfd);
    f(mEpollfd);

    // close DNS request manager
    spdlog::debug("[NetMgr::closeEnv] close DNS request manager");
    mDnsReqMgr.close();
}

void NetMgr::onSoc(time_t curTime, epoll_event &event)
{
    Endpoint *pEndpoint = static_cast<Endpoint *>(event.data.ptr);
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
        spdlog::trace("[NetMgr::onSoc] endpoint[{}] broken: {}", pEndpoint->toStr(), ss.str());

        pEndpoint->valid = false;
        if (pEndpoint->type & (Endpoint::Type_t::NORTH | Endpoint::Type_t::SOUTH))
        {
            pEndpoint->valid = false;
            Session *pSession = static_cast<Session *>(pEndpoint->tag);
            switch (pSession->getStatus())
            {
            case Session::State_t::CLOSE:
                break;
            case Session::State_t::CONNECTING:
            case Session::State_t::ESTABLISHED:
                pSession->setStatus(Session::State_t::CLOSE);
                break;
            default:
                spdlog::critical("[NetMgr::onSoc] invalid session status: {}.",
                                 pSession->getStatus());
                assert(false);
            }
            mPostProcessList.push_back(static_cast<Session *>(pEndpoint->tag));
        }
        else
        {
            spdlog::error("[NetMgr::onSoc] service endpoint[{}] broken", pEndpoint->toStr());
        }

        return;
    }

    switch (pEndpoint->type)
    {
    case Endpoint::Type_t::SERVICE:
        onService(curTime, event.events, pEndpoint);
        break;
    case Endpoint::Type_t::NORTH:
    case Endpoint::Type_t::SOUTH:
    {
        Session *pSession = static_cast<Session *>(pEndpoint->tag);
        // spdlog::trace("[NetMgr::onSoc] Session: {}", pSession->toStr());

        pSession->onSoc(curTime, pEndpoint, event.events);
    }
    break;
    default:
        spdlog::critical("[NetMgr::onSoc] invalid endpoint type: {}, event.data.ptr: {}.",
                         pEndpoint->type, event.data.ptr);
        assert(false);
    }
}

void NetMgr::onService(time_t curTime, uint32_t events, Endpoint *pEndpoint)
{
    if (events & EPOLLIN)
    {
        acceptClient(curTime, pEndpoint);
    }
    if (events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR))
    {
        stringstream ss;
        if (events & EPOLLRDHUP)
        {
            // socket has been closed
            ss << "closed by peer";
        }
        if (events & EPOLLHUP)
        {
            // socket has been closed
            ss << "has been closed";
        }
        if (events & EPOLLERR)
        {
            // socket error
            ss << "socket error";
        }

        spdlog::error("[NetMgr::onService] service endpoint[{}] fail: {}", pEndpoint->toStr(), ss.str());
    }
}

void NetMgr::acceptClient(time_t curTime, Endpoint *pEndpoint)
{
    uint32_t index = ptrToServiceIndex(pEndpoint->tag);

    // accept client
    struct sockaddr_in address;
    int addrlen = sizeof(address);

    int southSoc = accept(pEndpoint->soc, (struct sockaddr *)&address, (socklen_t *)&addrlen);
    if (southSoc == -1)
    {
        spdlog::error("[NetMgr::acceptClient] accept fail: {} - {}", errno, strerror(errno));
        return;
    }

    char ip[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &address.sin_addr, ip, INET_ADDRSTRLEN);

    int northSoc;
    // set socket to non-blocking mode
    if (fcntl(southSoc, F_SETFL, O_NONBLOCK) < 0)
    {
        spdlog::error("[NetMgr::acceptClient] set client socket to non-blocking mode fail. {}: {}",
                      errno, strerror(errno));
        close(southSoc);
        return;
    }

    northSoc = createNorthSoc(&mMapDatas[index]);
    if (northSoc == -1)
    {
        spdlog::error("[NetMgr::acceptClient] create to host socket fail");
        close(southSoc);
        return;
    }

    // alloc session object
    Session *pSession = mSessionMgr.alloc();
    if (!pSession)
    {
        spdlog::error("[NetMgr::acceptClient] alloc session object fail.");
        close(southSoc);
        close(northSoc);
        return;
    }

    // init session object
    using namespace std::placeholders;
    if (pSession->init(northSoc, southSoc,
                       std::bind(&NetMgr::joinEpoll, this, _1, _2, _3),
                       std::bind(&NetMgr::resetEpollMode, this, _1, _2, _3),
                       std::bind(&NetMgr::onSessionStatus, this, _1)))
    {
        spdlog::debug("[NetMgr::acceptClient] Accept client[{}:{}] {}:{} --> {}",
                      southSoc,
                      northSoc,
                      ip,
                      ntohs(address.sin_port),
                      mMapDatas[index].toStr());
    }
    else
    {
        spdlog::error("[NetMgr::acceptClient] init session object fail.");
        close(southSoc);
        close(northSoc);
        return;
    }
}

int NetMgr::createNorthSoc(MapData_t *pMapData)
{
    int soc = 0;
    struct sockaddr_in serv_addr;
    if ((soc = socket(AF_INET, SOCK_STREAM, 0)) < 0)
    {
        spdlog::error("[NetMgr::createNorthSoc] socket creation error{}: {}",
                      errno, strerror(errno));
        return -1;
    }

    // set socket to non-blocking mode
    if (fcntl(soc, F_SETFL, O_NONBLOCK) < 0)
    {
        spdlog::error("[NetMgr::createNorthSoc] set socket to non-blocking mode fail. {}: {}",
                      errno, strerror(errno));
    }
    else
    {
        serv_addr.sin_family = AF_INET;
        serv_addr.sin_port = htons(pMapData->hostPort);

        // Convert IPv4 and IPv6 addresses from text to binary form
        if (inet_pton(AF_INET, pMapData->host, &serv_addr.sin_addr) <= 0)
        {
            spdlog::error("[NetMgr::createNorthSoc] Can't convert host address[{}]. {}: {}",
                          errno, strerror(errno));
        }
        else
        {

            if (connect(soc, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0 &&
                errno != EALREADY && errno != EINPROGRESS)
            {
                spdlog::error("[NetMgr::createNorthSoc] Connection Failed{}: {}",
                              errno, strerror(errno));
            }
            else
            {
                return soc;
            }
        }
    }

    close(soc);
    return -1;
}

void NetMgr::postProcess(time_t curTime)
{
    if (!mPostProcessList.empty())
    {
        // 去重
        set<Session *> sessions;
        for (auto *pSession : mPostProcessList)
        {
            if (sessions.find(pSession) == sessions.end())
            {
                // spdlog::trace("[NetMgr::postProcess] post-processed for {}",
                //               pSession->toStr());
                sessions.insert(pSession);
            }
            // else
            // {
            //     spdlog::trace("[NetMgr::postProcess] skip duplicated session {}-{}",
            //                   pSession->mSouthEndpoint.soc,
            //                   pSession->mNorthEndpoint.soc);
            // }
        }
        mPostProcessList.clear();

        // 进行会话状态处理
        for (Session *pSession : sessions)
        {
            switch (pSession->getStatus())
            {
            case Session::State_t::CONNECTING:
                if (pSession->valid())
                {
                    // add into timeout timer
                    mConnectTimeoutContainer.insert(curTime, pSession);
                }
                else
                {
                    pSession->setStatus(Session::State_t::CLOSE);
                    onClose(pSession);
                }
                break;
            case Session::State_t::ESTABLISHED:
                mConnectTimeoutContainer.remove(pSession);
                if (pSession->valid())
                {
                    mSessionTimeoutContainer.insert(curTime, pSession);
                }
                else
                {
                    pSession->setStatus(Session::State_t::CLOSE);
                    onClose(pSession);
                }
                break;
            case Session::State_t::CLOSE:
            {
                TimeoutContainer *pContainer = pSession->getContainer();
                if (pContainer)
                {
                    pContainer->remove(pSession);
                }
                onClose(pSession);
            }
            break;
            default:
                spdlog::critical("[NetMgr::postProcess] invalid status: {}", pSession->getStatus());
                assert(false);
            }
        }
    }

    // timeout check
    timeoutCheck(curTime);
}

void NetMgr::onClose(Session *pSession)
{
    if ((pSession->mpToNorthBuffer->empty() ||
         !pSession->mNorthEndpoint.valid) &&
        (pSession->mpToSouthBuffer->empty() ||
         !pSession->mSouthEndpoint.valid))
    {
        // release session object
        spdlog::debug("[NetMgr::onClose] close session[{}-{}]",
                      pSession->mSouthEndpoint.soc, pSession->mNorthEndpoint.soc);
        removeAndCloseSoc(pSession->mSouthEndpoint.soc);
        removeAndCloseSoc(pSession->mNorthEndpoint.soc);

        // release session object
        mSessionMgr.free(pSession);
    }
    else if (!pSession->mpToNorthBuffer->empty() &&
             pSession->mNorthEndpoint.valid)
    {
        // send last data to client
        if (!resetEpollMode(&pSession->mNorthEndpoint, false, true))
        {
            spdlog::error("[NetMgr::onClose] Failed to modify north sock[{}] in epoll for last data. {} - {}",
                          pSession->mNorthEndpoint.soc, errno, strerror(errno));
            pSession->mNorthEndpoint.valid = false;
            onClose(pSession);
        }
    }
    else
    {
        assert(!pSession->mpToSouthBuffer->empty() &&
               pSession->mSouthEndpoint.valid);

        // send last data to client
        if (!resetEpollMode(&pSession->mSouthEndpoint, false, true))
        {
            spdlog::error("[NetMgr::onClose] Failed to modify south sock[{}] in epoll for last data. {} - {}",
                          pSession->mSouthEndpoint.soc, errno, strerror(errno));
            pSession->mSouthEndpoint.valid = false;
            onClose(pSession);
        }
    }
}

void NetMgr::removeAndCloseSoc(int sock)
{
    // spdlog::trace("[NetMgr::removeAndCloseSoc] remove sock[{}]", sock);

    // remove from epoll driver
    if (epoll_ctl(mEpollfd, EPOLL_CTL_DEL, sock, nullptr))
    {
        spdlog::error("[NetMgr::removeAndCloseSoc] remove sock[{}] from epoll fail. {} - {}",
                      sock, errno, strerror(errno));
    }

    // close socket
    if (close(sock))
    {
        spdlog::error("[NetMgr::removeAndCloseSoc] Close sock[{}] fail. {} - {}",
                      sock, errno, strerror(errno));
    }
}

bool NetMgr::joinEpoll(Endpoint *pEndpoint, bool read, bool write)
{
    // spdlog::trace("[NetMgr::joinEpoll] endpoint[{}], read[{}], write[{}]", pEndpoint->toStr(), read, write);

    struct epoll_event event;
    event.data.ptr = pEndpoint;
    event.events = EPOLLET | EPOLLRDHUP | (read ? EPOLLIN : 0) | (write ? EPOLLOUT : 0);
    if (epoll_ctl(mEpollfd, EPOLL_CTL_ADD, pEndpoint->soc, &event))
    {
        spdlog::error("[NetMgr::joinEpoll] events[{}]-soc[{}] join fail. Error {}: {}",
                      event.events, pEndpoint->soc, errno, strerror(errno));
        return false;
    }

    return true;
}

bool NetMgr::resetEpollMode(Endpoint *pEndpoint, bool read, bool write)
{
    // spdlog::trace("[NetMgr::resetEpollMode] endpoint[{}], read[{}], write[{}]", pEndpoint->toStr(), read, write);

    epoll_event event;
    event.data.ptr = pEndpoint;
    write = true;
    event.events = EPOLLET | EPOLLRDHUP | (read ? EPOLLIN : 0) | (write ? EPOLLOUT : 0);
    if (epoll_ctl(mEpollfd, EPOLL_CTL_MOD, pEndpoint->soc, &event))
    {
        spdlog::error("[NetMgr::resetEpollMode] Reset events[{}] for soc[{}] fail. {} - {}",
                      event.events, pEndpoint->soc, errno, strerror(errno));
        return false;
    }

    return true;
}

void *NetMgr::serviceIndexToPtr(uint32_t index)
{
    Converter_t conv;
    conv.u32 = index;
    return conv.ptr;
}

uint32_t NetMgr::ptrToServiceIndex(void *p)
{
    Converter_t conv;
    conv.ptr = p;
    return conv.u32;
}

void NetMgr::onSessionStatus(Session *pSession)
{
    mPostProcessList.push_back(pSession);
}

void NetMgr::timeoutCheck(time_t curTime)
{
    auto fn = [](time_t curTime,
                 uint64_t timeoutInterval,
                 TimeoutContainer &container,
                 const char *containerName) {
        TimeoutContainer::ContainerType timeoutClients =
            container.removeTimeout(curTime - timeoutInterval);
        for (auto *pClient : timeoutClients)
        {
            Session *pSession = static_cast<Session *>(pClient);
            spdlog::debug("[NetMgr::timeoutCheck] session[{}]@{} timeout.", pSession->toStr(), containerName);
            pSession->setStatus(Session::State_t::CLOSE);
        }
    };

    if (!mConnectTimeoutContainer.empty())
    {
        fn(curTime, mConnectTimeout, mConnectTimeoutContainer, "CONN");
    }
    if (!mSessionTimeoutContainer.empty())
    {
        fn(curTime, mSessionTimeout, mSessionTimeoutContainer, "ESTB");
    }
}

} // namespace mapper
