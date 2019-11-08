#include "netMgr.h"

#include <assert.h>
#include <errno.h>
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
            int64_t curTime;
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
                    list<Endpoint *> failedEndpoints;
                    for (int i = 0; i < nRet; ++i)
                    {
                        onSoc(curTime, events[i], failedEndpoints);
                    }

                    // 找出对应的异常会话对象，并去掉重复的会话对象以及非会话链路
                    set<Session *> failSessions;
                    for (auto *pEndpoint : failedEndpoints)
                    {
                        if (pEndpoint->type == Endpoint::Type_t::SERVICE)
                        {
                            continue;
                        }

                        Session *pSession = static_cast<Session *>(pEndpoint->tag);

                        if (failSessions.find(pSession) == failSessions.end())
                        {
                            spdlog::debug("remove processed {}-{}",
                                          pSession->mSouthEndpoint.soc,
                                          pSession->mNorthEndpoint.soc);
                            failSessions.insert(pSession);
                        }
                        else
                        {
                            spdlog::trace("skip duplicated session {}-{}",
                                          pSession->mSouthEndpoint.soc,
                                          pSession->mNorthEndpoint.soc);
                        }
                    }

                    // 针对找出的异常会话进行错误处理
                    for (auto *pSession : failSessions)
                    {
                        onFail(pSession);
                    }
                }

                // post-process
                postProcess(curTime);
            }
        }
        catch (const exception &e)
        {
            spdlog::error("[NetMgr::threadFunc] catch an exception. {}", e.what());
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

void NetMgr::onSoc(int64_t curTime, epoll_event &event, list<Endpoint *> &failedEndpoints)
{
    Endpoint *pEndpoint = static_cast<Endpoint *>(event.data.ptr);

    if (event.events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR))
    {
        // connection broken
        spdlog::debug("[NetMgr::onSoc] soc[{}] broken", pEndpoint->soc);
        pEndpoint->valid = false;
        if (pEndpoint->type & (Endpoint::Type_t::NORTH | Endpoint::Type_t::SOUTH))
        {
            static_cast<Session *>(pEndpoint->tag)->mStatus = Session::State_t::FAIL;
        }
        failedEndpoints.push_back(pEndpoint);
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
        if (!pSession->onSoc(pEndpoint, event.events, mEpollfd))
        {
            // 错误/失败处理
            pSession->mStatus = Session::State_t::FAIL;
            failedEndpoints.push_back(pEndpoint);
        }
    }
    break;
    default:
        spdlog::error("[NetMgr::onSoc] invalid endpoint type: {}, event.data.ptr: {}, skip it.",
                      pEndpoint->type, event.data.ptr);
        // assert(false);
    }
}

void NetMgr::onService(int64_t curTime, uint32_t events, Endpoint *pEndpoint)
{
    if (events & EPOLLIN)
    {
        acceptClient(pEndpoint);
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

void NetMgr::acceptClient(Endpoint *pEndpoint)
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
    if ([&]() -> bool {
            // set socket to non-blocking mode
            if (fcntl(southSoc, F_SETFL, O_NONBLOCK) < 0)
            {
                spdlog::error("[NetMgr::acceptClient] set client socket to non-blocking mode fail. {}: {}",
                              errno, strerror(errno));
                return false;
            }

            northSoc = createNorthSoc(&mMapDatas[index]);
            if (northSoc == -1)
            {
                spdlog::error("[NetMgr::acceptClient] create to host socket fail");
                return false;
            }

            // alloc session object
            Session *pSession = mSessionMgr.alloc(northSoc, southSoc);
            if (!pSession)
            {
                spdlog::error("[NetMgr::acceptClient] alloc session object fail.");
                return false;
            }

            struct epoll_event event;

            // 会话建立后再加入读写探测
            event.data.ptr = &pSession->mSouthEndpoint;
            assert(pSession->mSouthEndpoint.check());
            event.events = EPOLLRDHUP | EPOLLET;
            if (epoll_ctl(mEpollfd, EPOLL_CTL_ADD, southSoc, &event))
            {
                spdlog::error("[NetMgr::acceptClient] Failed to add client fd to epoll. Error{}: {}",
                              errno, strerror(errno));
                event.data.ptr = nullptr;
                return false;
            }

            // add north soc into epoll
            event.data.ptr = &pSession->mNorthEndpoint;
            assert(pSession->mNorthEndpoint.check());
            event.events = EPOLLOUT | EPOLLET | EPOLLRDHUP;
            if (epoll_ctl(mEpollfd, EPOLL_CTL_ADD, northSoc, &event))
            {
                spdlog::error("[NetMgr::acceptClient] Failed to add host fd to epoll. Error{}: {}",
                              errno, strerror(errno));
                event.data.ptr = nullptr;
                return false;
            }

            pSession->mStatus = Session::State_t::CONNECTING;

            return true;
        }())
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
        close(southSoc);
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

void NetMgr::postProcess(int64_t curTime)
{
    // TODO: remove timeout sessions
}

void NetMgr::onFail(Endpoint *pEndpoint)
{
    assert(pEndpoint->type & (Endpoint::Type_t::NORTH | Endpoint::Type_t::SOUTH));
    onFail(static_cast<Session *>(pEndpoint->tag));
}

void NetMgr::onFail(Session *pSession)
{
    assert(pSession->mStatus == Session::State_t::CLOSE ||
           pSession->mStatus == Session::State_t::FAIL);

    assert(pSession->mNorthEndpoint.valid == false ||
           pSession->mSouthEndpoint.valid == false);

    if (pSession->mNorthEndpoint.valid == false &&
        pSession->mSouthEndpoint.valid == false)
    {
        // close sockets and remove them from epoll
        spdlog::debug("[NetMgr::onFail] close session[{}-{}]",
                      pSession->mNorthEndpoint.soc, pSession->mSouthEndpoint.soc);
        removeAndCloseSoc(pSession->mNorthEndpoint.soc);
        removeAndCloseSoc(pSession->mSouthEndpoint.soc);

        // release session object
        mSessionMgr.free(pSession);
    }
    else if (pSession->mSouthEndpoint.valid)
    {
        if (pSession->mpToSouthBuffer->empty())
        {
            // no more data need to be sent, remove session
            pSession->mSouthEndpoint.valid = false;
            onFail(pSession);
        }
        else
        {
            // send last data to client
            assert(pSession->mSouthEndpoint.check());
            if (!resetEpollMode(pSession->mSouthEndpoint.soc,
                                EPOLLOUT | EPOLLET | EPOLLRDHUP,
                                &pSession->mSouthEndpoint))
            {
                spdlog::error("[NetMgr::onFail] Failed to modify south sock[{}] in epoll. Error{}: {}",
                              pSession->mSouthEndpoint.soc, errno, strerror(errno));
                pSession->mSouthEndpoint.valid = false;
                onFail(pSession);
            }
        }
    }
    else // pSession->mNorthEndpoint.valid
    {
        if (pSession->mpToNorthBuffer->empty())
        {
            // no more data need to be sent, remove session
            pSession->mNorthEndpoint.valid = false;
            onFail(pSession);
        }
        else
        {
            // send last data to client
            assert(pSession->mNorthEndpoint.check());
            if (!resetEpollMode(pSession->mNorthEndpoint.soc,
                                EPOLLOUT | EPOLLET | EPOLLRDHUP,
                                &pSession->mNorthEndpoint))
            {
                spdlog::error("[NetMgr::onFail] Failed to modify north sock[{}] in epoll. Error{}: {}",
                              pSession->mNorthEndpoint.soc, errno, strerror(errno));
                pSession->mNorthEndpoint.valid = false;
                onFail(pSession);
            }
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

bool NetMgr::resetEpollMode(int soc, uint32_t mode, void *tag)
{
    assert(soc);
    spdlog::trace("[Session::resetEpollMode] soc[{}], mode[{}], tag[{}]", soc, mode, tag);

    epoll_event event;
    event.data.ptr = tag;
    event.events = mode;
    if (epoll_ctl(mEpollfd, EPOLL_CTL_MOD, soc, &event))
    {
        spdlog::error("[NetMgr::resetEpollMode] Reset mode[{}] for soc[{}] fail. Error{}: {}",
                      mode, soc, errno, strerror(errno));
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

} // namespace mapper
