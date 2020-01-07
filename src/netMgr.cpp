#include "netMgr.h"

#include <errno.h>
#include <execinfo.h>
#include <sys/epoll.h>
#include <chrono>
#include <exception>
#include <spdlog/spdlog.h>
#include "link/tcpForwardService.h"
#include "link/udpForwardService.h"
#include "link/utils.h"

using namespace std;

namespace mapper
{

const int NetMgr::INTERVAL_EPOLL_RETRY = 100;
const int NetMgr::INTERVAL_CONNECT_RETRY = 7;
const int NetMgr::EPOLL_MAX_EVENTS = 128;

NetMgr::NetMgr()
    : mpCfg(nullptr),
      mEpollfd(0),
      mStopFlag(true)
{
}

NetMgr::~NetMgr()
{
    join();
}

bool NetMgr::start(config::Config &cfg)
{
    spdlog::debug("[NetMgr::start] start.");

    // read settings
    mpCfg = &cfg;
    mForwards = move(mpCfg->getForwards("mapping"));

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

    spdlog::debug("[NetMgr::join] finish.");
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
            time_t lastScanTime = 0;
            struct epoll_event events[EPOLL_MAX_EVENTS];
            set<link::Service *> activeServices;

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
                        link::Endpoint_t *pe = (link::Endpoint_t *)events[i].data.ptr;
                        link::Service *ps = (link::Service *)pe->service;
                        ps->onSoc(curTime, events[i].events, pe);

                        activeServices.insert(ps);
                    }

                    // post process
                    for (auto s : activeServices)
                    {
                        s->postProcess(curTime);
                    }
                    activeServices.clear();
                }

                // scan timeout
                if (lastScanTime < curTime)
                {
                    for (auto s : mServices)
                    {
                        s->scanTimeout(curTime);
                    }

                    lastScanTime = curTime;
                }
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
                    spdlog::error("[NetMgr::threadFunc] {}", strings[i]);
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

        link::Service *pService;
        link::Protocol_t protocol = link::Utils::parseProtocol(forward->protocol);

        if (protocol == link::Protocol_t::TCP)
        {
            pService = new link::TcpForwardService();
            if (pService == nullptr)
            {
                spdlog::error("[NetMgr::initEnv] create instance of Tcp Forward Service fail");
                return false;
            }
            if (!((link::TcpForwardService *)pService)
                     ->init(mEpollfd, forward, mpCfg->getLinkSharedBuffer()))
            {
                spdlog::error("[NetMgr::initEnv] init instance of Tcp Forward Service fail");
                delete pService;
                return false;
            }
        }
        else
        {
            pService = new link::UdpForwardService();
            if (pService == nullptr)
            {
                spdlog::error("[NetMgr::initEnv] create instance of Udp Forward Service fail");
                return false;
            }
            if (!((link::UdpForwardService *)pService)
                     ->init(mEpollfd, forward, mpCfg->getLinkUdpBuffer()))
            {
                spdlog::error("[NetMgr::initEnv] init instance of Udp Forward Service fail");
                delete pService;
                return false;
            }
        }

        mServices.push_back(pService);
        spdlog::info("[NetMgr::initEnv] forward[{}] -- soc[{}] -- {}",
                     index++,
                     pService->getServiceEndpoint().soc,
                     forward->toStr());
    }

    return true;
}

void NetMgr::closeEnv()
{
    if (!mServices.empty())
    {
        for (auto ps : mServices)
        {
            ps->close();
            delete ps;
        }
        mServices.clear();
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
}

} // namespace mapper
