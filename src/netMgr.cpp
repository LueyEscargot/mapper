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

using namespace std;

namespace mapper
{

const int NetMgr::INTERVAL_EPOLL_RETRY = 100;
const int NetMgr::INTERVAL_CONNECT_RETRY = 7;

NetMgr::NetMgr(uint32_t bufSize)
    : mEpollfd(0),
      mSessionMgr(bufSize),
      mStopFlag(true)
{
}

NetMgr::~NetMgr()
{
    stop();
}

bool NetMgr::start(const int maxSessions, vector<mapper::MapData_t> &mapDatas)
{
    if (!mapDatas.size())
    {
        spdlog::warn("[NetMgr::start] no mapping data!");
    }

    mMapDatas.swap(mapDatas);

    // start thread
    {
        spdlog::debug("[NetMgr::start] start thread");
        if (!mStopFlag)
        {
            spdlog::error("[NetMgr::start] stop thread first");
            return false;
        }

        if (!mSessionMgr.init(maxSessions))
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
                        spdlog::error("[NetMgr::threadFunc] epoll fail. Error{}: {}",
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
    if ([&]() -> bool {
            // create epoll
            spdlog::debug("[NetMgr::initEnv] create epoll");
            if ((mEpollfd = epoll_create1(0)) < 0)
            {
                spdlog::error("[NetMgr::initEnv] Failed to create epoll. Error{}: {}",
                              errno, strerror(errno));
                return false;
            }

            int index = 0;
            for (auto mapData : mMapDatas)
            {
                spdlog::debug("[NetMgr::initEnv] process map data entry: {}", mapData.toStr());

                // create service socket
                int soc = socket(AF_INET, SOCK_STREAM, 0);
                if (soc <= 0)
                {
                    spdlog::error("[NetMgr::initEnv] Fail to create service socket. Error{}: {}",
                                  errno, strerror(errno));
                    return false;
                }

                // set reuse
                int opt = 1;
                if (setsockopt(soc, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &opt, sizeof(opt)))
                {
                    spdlog::error("[NetMgr::initEnv] Fail to reuse server socket. Error{}: {}",
                                  errno, strerror(errno));
                    return false;
                }

                // bind
                struct sockaddr_in addr;
                memset(&addr, 0, sizeof(addr));
                addr.sin_family = AF_INET;
                addr.sin_addr.s_addr = INADDR_ANY;
                addr.sin_port = htons(mapData.port);
                if (bind(soc, (struct sockaddr *)&addr, sizeof(addr)))
                {
                    spdlog::error("[NetMgr::initEnv] bind to 0.0.0.0:{} fail: {} - {}",
                                  mapData.port, errno, strerror(errno));
                    return false;
                }

                // listen
                if (listen(soc, SOMAXCONN) == -1)
                {
                    spdlog::error("[NetMgr::initEnv] Listen at 0.0.0.0:{} fail: {} - {}",
                                  mapData.port, errno, strerror(errno));
                    return false;
                }

                // save service endpoint
                Endpoint *pEndpoint = new Endpoint(Endpoint::Type_t::SERVICE, soc, serviceIndexToPtr(index));
                if (pEndpoint == nullptr)
                {
                    spdlog::error("[NetMgr::initEnv] create endpoint object fail: {} - {}",
                                  errno, strerror(errno));
                    return false;
                }
                mSvrEndpoints.emplace_back(pEndpoint);

                // add service endpoint into epoll driver
                struct epoll_event event;
                event.events = EPOLLIN | EPOLLOUT | EPOLLRDHUP;
                event.data.ptr = pEndpoint;
                assert(pEndpoint->check());
                if (epoll_ctl(mEpollfd, EPOLL_CTL_ADD, soc, &event))
                {
                    spdlog::error("[NetMgr::initEnv] add service endpoint into epoll fail. Error{}: {}",
                                  errno, strerror(errno));
                    return false;
                }

                ++index;
                spdlog::info("[NetMgr::initEnv] map[{}]: socket[{}] - 0.0.0.0:{} --> {}:{}",
                             index, soc, mapData.port, mapData.host, mapData.hostPort);
            }

            return true;
        }())
    {
        return true;
    }
    else
    {
        closeEnv();
        return false;
    }
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

    // close epoll file descriptor
    spdlog::debug("[NetMgr::closeEnv] close epoll file descriptor");
    if (close(mEpollfd))
    {
        spdlog::error("[NetMgr::closeEnv] Fail to close epoll. Error{}: {}",
                      errno, strerror(errno));
    }
    mEpollfd = 0;
}

void NetMgr::onSoc(time_t curTime, epoll_event &event)
{
    Endpoint *pEndpoint = static_cast<Endpoint *>(event.data.ptr);
    // spdlog::trace("[NetMgr::onSoc] {}", pEndpoint->toStr());

    if (event.events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR))
    {
        // connection broken
        spdlog::debug("[NetMgr::onSoc] soc[{}] broken", pEndpoint->soc);
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
            spdlog::error("[NetMgr::onSoc] service soc[{}] broken", pEndpoint->soc);
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
    if (events & (EPOLLRDHUP | EPOLLHUP))
    {
        // socket has been closed
        spdlog::error("[NetMgr::onService] service soc-{} has been closed", pEndpoint->soc);
    }
    if (events & EPOLLERR)
    {
        // socket error
        spdlog::error("[NetMgr::onService] error detected at service soc-{}", pEndpoint->soc);
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
        close(northSoc);
        close(southSoc);
        return;
    }

    // init session object
    using namespace std::placeholders;
    if (pSession->init(northSoc, southSoc,
                       std::bind(&NetMgr::joinEpoll, this, _1, _2, _3),
                       std::bind(&NetMgr::resetEpollMode, this, _1, _2, _3),
                       std::bind(&NetMgr::onSessionStatus, this, _1)))
    {
        spdlog::debug("[NetMgr::acceptClient] Accept client[{}:{}-{}:{}]-->{}",
                      ip,
                      ntohs(address.sin_port),
                      southSoc,
                      northSoc,
                      mMapDatas[index].toStr());
    }
    else
    {
        spdlog::error("[NetMgr::acceptClient] init session object fail.");
        close(northSoc);
        close(southSoc);
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
                      pSession->mNorthEndpoint.soc, pSession->mSouthEndpoint.soc);
        removeAndCloseSoc(pSession->mNorthEndpoint.soc);
        removeAndCloseSoc(pSession->mSouthEndpoint.soc);

        // release session object
        mSessionMgr.free(pSession);
    }
    else if (!pSession->mpToNorthBuffer->empty() &&
             pSession->mNorthEndpoint.valid)
    {
        // send last data to client
        if (!resetEpollMode(&pSession->mNorthEndpoint, false, true))
        {
            spdlog::error("[NetMgr::onClose] Failed to modify north sock[{}] in epoll for last data. Error{}: {}",
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
            spdlog::error("[NetMgr::onClose] Failed to modify south sock[{}] in epoll for last data. Error{}: {}",
                          pSession->mSouthEndpoint.soc, errno, strerror(errno));
            pSession->mSouthEndpoint.valid = false;
            onClose(pSession);
        }
    }
}

void NetMgr::removeAndCloseSoc(int sock)
{
    // remove from epoll driver
    if (epoll_ctl(mEpollfd, EPOLL_CTL_DEL, sock, nullptr))
    {
        spdlog::error("[NetMgr::removeAndCloseSoc] remove sock[{}] from epoll fail. Error{}: {}",
                      sock, errno, strerror(errno));
    }

    // close socket
    if (close(sock))
    {
        spdlog::error("[NetMgr::removeAndCloseSoc] Close sock[{}] fail. Error{}: {}",
                      sock, errno, strerror(errno));
    }
}

bool NetMgr::joinEpoll(Endpoint *pEndpoint, bool read, bool write)
{
    // spdlog::trace("[NetMgr::joinEpoll] soc[{}], read[{}], write[{}]", pEndpoint->soc, read, write);

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
    // spdlog::trace("[NetMgr::resetEpollMode] soc[{}], read[{}], write[{}]", pEndpoint->soc, read, write);

    epoll_event event;
    event.data.ptr = pEndpoint;
    write = true;
    event.events = EPOLLET | EPOLLRDHUP | (read ? EPOLLIN : 0) | (write ? EPOLLOUT : 0);
    if (epoll_ctl(mEpollfd, EPOLL_CTL_MOD, pEndpoint->soc, &event))
    {
        spdlog::error("[NetMgr::resetEpollMode] Reset events[{}] for soc[{}] fail. Error{}: {}",
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
                 const char * containerName) {
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
        fn(curTime, CONNECT_TIMEOUT, mConnectTimeoutContainer, "CONN");
    }
    if (!mSessionTimeoutContainer.empty())
    {
        fn(curTime, SESSION_TIMEOUT, mSessionTimeoutContainer, "ESTB");
    }
}

} // namespace mapper
