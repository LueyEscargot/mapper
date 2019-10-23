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
#include <spdlog/spdlog.h>
#include "session.h"

using namespace std;

namespace mapper
{

const int NetMgr::INTERVAL_EPOLL_RETRY = 100;
const int NetMgr::INTERVAL_CONNECT_RETRY = 7;

NetMgr::NetMgr()
    : mpMapDatas(nullptr),
      mEpollfd(0),
      mStopFlag(true)
{
}

NetMgr::~NetMgr()
{
    stop();
}

bool NetMgr::start(const int maxSessions, vector<mapper::MapData_t> *pMapDatas)
{
    assert(pMapDatas && pMapDatas->size());
    mpMapDatas = pMapDatas;

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
    // create epoll
    spdlog::debug("[NetMgr::initEnv] create epoll");
    if ((mEpollfd = epoll_create1(0)) < 0)
    {
        spdlog::error("[NetMgr::initEnv] Failed to create epoll. Error{}: {}",
                      errno, strerror(errno));
        closeEnv();
        return false;
    }

    for (auto mapData : *mpMapDatas)
    {
        spdlog::debug("[NetMgr::initEnv] process map data entry: {}", mapData.toStr());

        assert(strlen(mapData.host));

        // create service socket
        int soc = socket(AF_INET, SOCK_STREAM, 0);
        if (soc <= 0)
        {
            spdlog::error("[NetMgr::initEnv] Fail to create service socket. Error{}: {}",
                          errno, strerror(errno));
            closeEnv();
            return false;
        }

        // set reuse
        int opt = 1;
        if (setsockopt(soc, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &opt, sizeof(opt)))
        {
            spdlog::error("[NetMgr::initEnv] Fail to reuse server socket. Error{}: {}",
                          errno, strerror(errno));
            closeEnv();
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
            closeEnv();
            return false;
        }

        // listen
        if (listen(soc, SOMAXCONN) == -1)
        {
            spdlog::error("[NetMgr::initEnv] Listen at 0.0.0.0:{} fail: {} - {}",
                          mapData.port, errno, strerror(errno));
            closeEnv();
            return false;
        }

        // save service socket
        SockSvr_t *pSoc = new SockSvr_t(soc, mapData.port, mapData.host, mapData.hostPort);
        mpSvrSocs.push_back(pSoc);

        // add service socket into epoll driver
        struct epoll_event event;
        event.events = EPOLLIN | EPOLLOUT | EPOLLRDHUP;
        event.data.ptr = pSoc;
        if (epoll_ctl(mEpollfd, EPOLL_CTL_ADD, soc, &event))
        {
            spdlog::error("[NetMgr::initEnv] add service socket into epoll fail. Error{}: {}",
                          errno, strerror(errno));
            closeEnv();
            return false;
        }

        spdlog::info("[NetMgr::initEnv] {} - 0.0.0.0:{} --> {}:{}",
                     soc, mapData.port, mapData.host, mapData.hostPort);
    }

    return true;
}

void NetMgr::closeEnv()
{
    // close service socket
    spdlog::debug("[NetMgr::closeEnv] close service socket");
    for (auto *pSoc : mpSvrSocs)
    {
        // remove from epoll driver
        if (epoll_ctl(mEpollfd, EPOLL_CTL_DEL, pSoc->soc, nullptr))
        {
            spdlog::error("[NetMgr::closeEnv] remove service socket[{}] from epoll fail. Error{}: {}",
                          pSoc->soc, errno, strerror(errno));
        }

        // close socket
        if (close(pSoc->soc))
        {
            spdlog::error("[NetMgr::closeEnv] Close service socket fail. Error{}: {}",
                          errno, strerror(errno));
        }

        delete pSoc;
    }
    mpSvrSocs.clear();

    // close epoll file descriptor
    spdlog::debug("[NetMgr::closeEnv] close epoll file descriptor");
    if (mEpollfd)
    {
        if (close(mEpollfd))
        {
            spdlog::error("[NetMgr::closeEnv] Fail to close epoll. Error{}: {}",
                          errno, strerror(errno));
        }
        mEpollfd = 0;
    }
}

void NetMgr::onSoc(int64_t curTime, epoll_event &event)
{
    SOCK_BASE *pSoc = static_cast<SOCK_BASE *>(event.data.ptr);
    switch (pSoc->type)
    {
    case SOCK_TYPE::SVR_SOCK:
        onSvrSoc(curTime, event.events, static_cast<SockSvr_t *>(event.data.ptr));
        break;
    case SOCK_TYPE::HOST_SOCK:
        onHostSoc(curTime, event.events, static_cast<SockHost_t *>(event.data.ptr));
        break;
    case SOCK_TYPE::CLIENT_SOCK:
        onClientSoc(curTime, event.events, static_cast<SockClient_t *>(event.data.ptr));
        break;
    default:
        spdlog::critical("[NetMgr::threadFunc] invalid sock type: {}", pSoc->type);
        assert(false);
    }
}

void NetMgr::onSvrSoc(int64_t curTime, uint32_t events, SockSvr_t *pSoc)
{
    if (events & EPOLLIN)
    {
        acceptClient(pSoc);
    }
    if (events & (EPOLLRDHUP | EPOLLHUP))
    {
        // socket has been closed
        spdlog::error("[NetMgr::onSvrSoc] service soc-{} has been closed", pSoc->soc);
    }
    if (events & EPOLLERR)
    {
        // socket error
        spdlog::error("[NetMgr::onSvrSoc] error detected at service soc-{}", pSoc->soc);
    }
}

void NetMgr::onHostSoc(int64_t curTime, uint32_t events, SockHost_t *pSoc)
{
    Session_t *pSession = pSoc->pSession;

    if (events & (EPOLLRDHUP | EPOLLHUP))
    {
        // socket has been closed
        spdlog::debug("[NetMgr::onHostSoc] host soc-{} has been closed", pSoc->soc);
        pSession->status = STATE_MACHINE::FAIL;
        pSession->toHostSockFail = true;
        onFail(pSession);
        return;
    }
    if (events & EPOLLERR)
    {
        // socket error
        spdlog::error("[NetMgr::onHostSoc] error detected on host soc-{}", pSoc->soc);
        pSession->status = STATE_MACHINE::FAIL;
        pSession->toHostSockFail = true;
        onFail(pSession);
        return;
    }
    // read action
    if (events & EPOLLIN)
    {
        if (!Session::onHostSockRecv(pSession))
        {
            // receive data from host fail
            onFail(pSession);
            return;
        }

        if (pSession->fullFlag2Client && !pSession->buffer2Client.full())
        {
            // enable host soc recv data again
            pSession->fullFlag2Client = false;
            epoll_event event;
            event.data.ptr = pSoc;
            event.events = EPOLLIN | EPOLLOUT | EPOLLET | EPOLLRDHUP;
            if (epoll_ctl(mEpollfd, EPOLL_CTL_MOD, pSoc->soc, &event))
            {
                spdlog::error("[NetMgr::onHostSoc] Failed to modify host soc[{}] in epoll. Error{}: {}",
                              pSoc->soc, errno, strerror(errno));
                pSession->status == STATE_MACHINE::FAIL;
                pSession->toHostSockFail = true;
                onFail(pSession);
                return;
            }
        }
    }
    // write action
    if (events & EPOLLOUT)
    {
        if (!Session::onHostSockSend(pSession, mEpollfd))
        {
            // send data to host fail
            onFail(pSession);
            return;
        }

        if (pSession->fullFlag2Host && !pSession->buffer2Host.full())
        {
            // enable client soc recv data again
            pSession->fullFlag2Host = false;
            epoll_event event;
            event.data.ptr = pSoc;
            event.events = EPOLLIN | EPOLLOUT | EPOLLET | EPOLLRDHUP;
            if (epoll_ctl(mEpollfd, EPOLL_CTL_MOD, pSoc->soc, &event))
            {
                spdlog::error("[NetMgr::onClientSoc] Failed to modify client soc[{}] in epoll. Error{}: {}",
                              pSoc->soc, errno, strerror(errno));
                pSession->status == STATE_MACHINE::FAIL;
                pSession->toClientSockFail = true;
                onFail(pSession);
                return;
            }
        }
    }
}

void NetMgr::onClientSoc(int64_t curTime, uint32_t events, SockClient_t *pSoc)
{
    Session_t *pSession = pSoc->pSession;

    if (events & (EPOLLRDHUP | EPOLLHUP))
    {
        // socket has been closed
        spdlog::debug("[NetMgr::onClientSoc] client soc-{} has been closed", pSoc->soc);
        pSession->status = STATE_MACHINE::FAIL;
        pSession->toClientSockFail = true;
        onFail(pSession);
        return;
    }
    if (events & EPOLLERR)
    {
        // socket error
        spdlog::error("[NetMgr::onClientSoc] error detected on client soc-{}", pSoc->soc);
        pSession->status = STATE_MACHINE::FAIL;
        pSession->toClientSockFail = true;
        onFail(pSession);
        return;
    }
    // read action
    if (events & EPOLLIN)
    {
        if (!Session::onClientSockRecv(pSession))
        {
            // receive data from client fail
            onFail(pSession);
            return;
        }

        if (pSession->fullFlag2Host && !pSession->buffer2Host.full())
        {
            // enable client soc recv data again
            pSession->fullFlag2Host = false;
            epoll_event event;
            event.data.ptr = pSoc;
            event.events = EPOLLIN | EPOLLOUT | EPOLLET | EPOLLRDHUP;
            if (epoll_ctl(mEpollfd, EPOLL_CTL_MOD, pSoc->soc, &event))
            {
                spdlog::error("[NetMgr::onClientSoc] Failed to modify client soc[{}] in epoll. Error{}: {}",
                              pSoc->soc, errno, strerror(errno));
                pSession->status == STATE_MACHINE::FAIL;
                pSession->toClientSockFail = true;
                onFail(pSession);
                return;
            }
        }
    }
    // write action
    if (events & EPOLLOUT)
    {
        if (!Session::onClientSockSend(pSession))
        {
            // send data to client fail
            onFail(pSession);
            return;
        }

        if (pSession->fullFlag2Client && !pSession->buffer2Client.full())
        {
            // enable host soc recv data again
            pSession->fullFlag2Client = false;
            epoll_event event;
            event.data.ptr = pSoc;
            event.events = EPOLLIN | EPOLLOUT | EPOLLET | EPOLLRDHUP;
            if (epoll_ctl(mEpollfd, EPOLL_CTL_MOD, pSoc->soc, &event))
            {
                spdlog::error("[NetMgr::onHostSoc] Failed to modify host soc[{}] in epoll. Error{}: {}",
                              pSoc->soc, errno, strerror(errno));
                pSession->status == STATE_MACHINE::FAIL;
                pSession->toHostSockFail = true;
                onFail(pSession);
                return;
            }
        }
    }
}

void NetMgr::acceptClient(SockSvr_t *pSoc)
{
    // accept client
    int soc = accept(pSoc->soc, NULL, NULL);
    if (soc == -1)
    {
        spdlog::error("[NetMgr::acceptClient] accept fail: {} - {}", errno, strerror(errno));
        return;
    }
    // set socket to non-blocking mode
    if (fcntl(soc, F_SETFL, O_NONBLOCK) < 0)
    {
        spdlog::error("[NetMgr::acceptClient] set client socket to non-blocking mode fail. {}: {}",
                      errno, strerror(errno));
        close(soc);
        return;
    }

    // create to host socket
    int hostSoc = createHostSoc(pSoc);
    if (hostSoc == -1)
    {
        spdlog::error("[NetMgr::acceptClient] create to host socket fail");
        close(soc);
        return;
    }
    // set socket to non-blocking mode
    if (fcntl(hostSoc, F_SETFL, O_NONBLOCK) < 0)
    {
        spdlog::error("[NetMgr::acceptClient] set host socket to non-blocking mode fail. {}: {}",
                      errno, strerror(errno));
        close(soc);
        close(hostSoc);
        return;
    }

    // alloc session object
    Session_t *pSession = mSessionMgr.allocSession();
    if (!pSession)
    {
        spdlog::error("[NetMgr::acceptClient] alloc session object fail.");
        close(soc);
        close(hostSoc);
    }
    pSession->init(soc, hostSoc);

    struct epoll_event event;

    // 会话建立后再加入读写探测
    event.data.ptr = &pSession->clientSoc;
    event.events = EPOLLRDHUP | EPOLLET;
    if (epoll_ctl(mEpollfd, EPOLL_CTL_ADD, soc, &event))
    {
        spdlog::error("[NetMgr::acceptClient] Failed to add client fd to epoll. Error{}: {}",
                      errno, strerror(errno));
        close(soc);
        close(hostSoc);
        mSessionMgr.freeSession(pSession);
    }

    // add host soc into epoll
    event.data.ptr = &pSession->hostSoc;
    event.events = EPOLLIN | EPOLLOUT | EPOLLET | EPOLLRDHUP;
    if (epoll_ctl(mEpollfd, EPOLL_CTL_ADD, hostSoc, &event))
    {
        spdlog::error("[NetMgr::acceptClient] Failed to add host fd to epoll. Error{}: {}",
                      errno, strerror(errno));
        close(soc);
        close(hostSoc);
        mSessionMgr.freeSession(pSession);
    }

    spdlog::debug("[NetMgr::acceptClient] Accept client-{} for [{}]", soc, pSoc->mapData.toStr());
}

int NetMgr::createHostSoc(SockSvr_t *pSoc)
{
    int soc = 0;
    struct sockaddr_in serv_addr;
    if ((soc = socket(AF_INET, SOCK_STREAM, 0)) < 0)
    {
        spdlog::error("[NetMgr::createHostSoc] socket creation error{}: {}",
                      errno, strerror(errno));
        return -1;
    }

    // set socket to non-blocking mode
    if (fcntl(soc, F_SETFL, O_NONBLOCK) < 0)
    {
        spdlog::error("[NetMgr::createHostSoc] set socket to non-blocking mode fail. {}: {}",
                      errno, strerror(errno));
        close(soc);
        return -1;
    }

    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(pSoc->mapData.hostPort);

    // Convert IPv4 and IPv6 addresses from text to binary form
    if (inet_pton(AF_INET, pSoc->mapData.host, &serv_addr.sin_addr) <= 0)
    {
        spdlog::error("[NetMgr::createHostSoc] Can't convert host address[{}]. {}: {}",
                      errno, strerror(errno));
        close(soc);
        return -1;
    }

    if (connect(soc, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0 &&
        errno != EALREADY && errno != EINPROGRESS)
    {
        spdlog::error("[NetMgr::createHostSoc] Connection Failed{}: {}",
                      errno, strerror(errno));
        close(soc);
        return -1;
    }

    return soc;
}

void NetMgr::postProcess(int64_t curTime)
{
    // TODO: remove timeout sessions
}

void NetMgr::onFail(Session_t *pSession)
{
    assert(pSession->status == STATE_MACHINE::FAIL);

    if (pSession->toHostSockFail && pSession->toHostSockFail)
    {
        // close sockets
        {
            int clientSoc = pSession->clientSoc.soc;
            int hostSoc = pSession->hostSoc.soc;

            spdlog::debug("[NetMgr::onFail] close session[{}-{}]", clientSoc, hostSoc);
            removeAndCloseSoc(clientSoc);
            removeAndCloseSoc(hostSoc);
        }

        // release session object
        mSessionMgr.freeSession(pSession);
    }
    else if (pSession->toHostSockFail)
    {
        if (pSession->buffer2Client.empty())
        {
            // no data need to be sent, remove session
            pSession->toClientSockFail = true;
            onFail(pSession);
        }
        else
        {
            // send last data to client
            epoll_event event;
            event.data.ptr = &pSession->clientSoc;
            event.events = EPOLLOUT | EPOLLET | EPOLLRDHUP;
            if (epoll_ctl(mEpollfd, EPOLL_CTL_MOD, pSession->clientSoc.soc, &event))
            {
                spdlog::error("[NetMgr::onFail] Failed to modify client sock[{}] in epoll. Error{}: {}",
                              pSession->clientSoc.soc, errno, strerror(errno));
                pSession->toClientSockFail = true;

                // for remove session
                onFail(pSession);
            }
        }
    }
    else if (pSession->toClientSockFail)
    {
        if (pSession->buffer2Host.empty())
        {
            // no data need to be sent, remove session
            pSession->toHostSockFail = true;
            onFail(pSession);
        }
        else
        {
            // send last data to host
            epoll_event event;
            event.data.ptr = &pSession->hostSoc;
            event.events = EPOLLOUT | EPOLLET | EPOLLRDHUP;
            if (epoll_ctl(mEpollfd, EPOLL_CTL_MOD, pSession->hostSoc.soc, &event))
            {
                spdlog::error("[NetMgr::onFail] Failed to modify host sock[{}] in epoll. Error{}: {}",
                              pSession->hostSoc.soc, errno, strerror(errno));
                pSession->toHostSockFail = true;

                // for remove session
                onFail(pSession);
            }
        }
    }
    else
    {
        spdlog::critical("[NetMgr::onFail] invalid logic");
        assert(false);
    }
}

void NetMgr::removeAndCloseSoc(int sock)
{
    // remove from epoll driver
    if (epoll_ctl(mEpollfd, EPOLL_CTL_DEL, sock, nullptr))
    {
        spdlog::error("[NetMgr::removeAndCloseSoc] remove client sock[{}] from epoll fail. Error{}: {}",
                      sock, errno, strerror(errno));
    }

    // close socket
    if (close(sock))
    {
        spdlog::error("[NetMgr::removeAndCloseSoc] Close client sock[{}] fail. Error{}: {}",
                      sock, errno, strerror(errno));
    }
}

} // namespace mapper
