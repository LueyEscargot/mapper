#include "service.h"
#include <string.h>
#include <sys/epoll.h>
#include <sstream>
#include <spdlog/spdlog.h>
#include "tcpForwardService.h"
#include "udpForwardService.h"
#include "../utils/jsonUtils.h"

#include "schema.def"

using namespace std;
using namespace rapidjson;
using namespace mapper::buffer;
using namespace mapper::utils;

#define ENABLE_DETAIL_LOGS
#undef ENABLE_DETAIL_LOGS

namespace mapper
{
namespace link
{

const string Service::CONFIG_BASE_PATH = "/service";

bool Service::create(Document &cfg, list<Service *> &serviceList)
{
    auto serviceCfg = JsonUtils::getObj(&cfg, CONFIG_BASE_PATH);
    if (!serviceCfg)
    {
        spdlog::error("[Service::create] config entity[{}] not exist", CONFIG_BASE_PATH);
        return false;
    }

    // validate by schema
    stringstream ss;
    if (!JsonUtils::validate(serviceCfg, SERVICE_SCHEMA, &ss))
    {
        spdlog::error("[Service::create] validate by schema fail: {}", ss.str());
        return false;
    }

    // load setting
    Setting_t setting;
    loadSetting(cfg, setting);

    // process forwards
    auto forwards = JsonUtils::getArray(serviceCfg, "/forward");
    if (!forwards)
    {
        spdlog::warn("[Service::create] no forwards has been found");
        return true;
    }

    list<shared_ptr<Forward>> tcpForwardList;
    list<shared_ptr<Forward>> udpForwardList;
    for (auto it = forwards->Begin(); it != forwards->End(); ++it)
    {
        if (it->GetType() == kStringType)
        {
            auto forwardStr = it->GetString();
            auto forward = Forward::create(forwardStr);
            if (forward)
            {
                auto protocol = Utils::parseProtocol(forward->protocol);
                if (protocol == PROTOCOL_TCP)
                {
                    // TCP Forward service
                    tcpForwardList.push_back(forward);
                }
                else if (protocol == PROTOCOL_UDP)
                {
                    // UDP Forward service
                    udpForwardList.push_back(forward);
                }
                else
                {
                    spdlog::error("[Service::create] unsupported protocol[{}]", forward->protocol);
                    Forward::release(forward);
                }
            }
            else
            {
                spdlog::warn("[TcpForwardService::create] skip invalid forward: {}", forwardStr);
            }
        }
    }

    if ([&]() {
            // init tcp forward service
            if (!tcpForwardList.empty())
            {
                auto pService = new TcpForwardService;
                if (pService)
                {
                    if (pService->init(tcpForwardList, setting))
                    {
                        serviceList.push_back(pService);
                    }
                    else
                    {
                        spdlog::error("[Service::create] init tcp forward service object fail");
                        delete pService;
                        return false;
                    }
                }
                else
                {
                    spdlog::error("[Service::create] create tcp forward service fail");
                    return false;
                }
            }

            // create udp forward service
            if (!udpForwardList.empty())
            {
                auto pService = new UdpForwardService;
                if (pService)
                {
                    if (pService->init(udpForwardList, setting))
                    {
                        serviceList.push_back(pService);
                    }
                    else
                    {
                        spdlog::error("[Service::create] init udp forward service object fail");
                        delete pService;
                        return false;
                    }
                }
                else
                {
                    spdlog::error("[Service::create] create udp forward service fail");
                    return false;
                }
            }

            return true;
        }())
    {
        return true;
    }
    else
    {
        release(serviceList);
        return false;
    }
}

void Service::release(list<Service *> &serviceList)
{
    for (auto pService : serviceList)
    {
        pService->close();
        delete pService;
    }
    serviceList.clear();
}

void Service::loadSetting(rapidjson::Document &cfg, Setting_t &setting)
{
    // timeout
    setting.connectTimeout =
        JsonUtils::getAsUint32(cfg,
                               CONFIG_BASE_PATH + "/setting/timeout/connect",
                               SEETING_TIMEOUT_CONNECT);
    setting.sessionTimeout =
        JsonUtils::getAsUint32(cfg,
                               CONFIG_BASE_PATH + "/setting/timeout/session",
                               SEETING_TIMEOUT_SESSION);
    setting.releaseTimeout =
        JsonUtils::getAsUint32(cfg,
                               CONFIG_BASE_PATH + "/setting/timeout/release",
                               SEETING_TIMEOUT_RELEASE);
    setting.udpTimeout =
        JsonUtils::getAsUint32(cfg,
                               CONFIG_BASE_PATH + "/setting/timeout/udp",
                               SEETING_TIMEOUT_UDP);
    // buffer
    setting.bufferSize =
        JsonUtils::getAsUint64(cfg,
                               CONFIG_BASE_PATH + "/setting/buffer/size",
                               SEETING_BUFFER_SIZE) *
        SEETING_BUFFER_SIZE_UNIT;
    setting.bufferPerSessionLimit =
        JsonUtils::getAsUint64(cfg,
                               CONFIG_BASE_PATH + "/setting/buffer/perSessionLimit",
                               SEETING_BUFFER_PERSESSIONLIMIT) *
        SEETING_BUFFER_SIZE_UNIT;
}

bool Service::epollAddEndpoint(int epollfd, Endpoint_t *pe, bool read, bool write, bool edgeTriger)
{
    struct epoll_event event;
    event.data.ptr = pe;
    event.events = EPOLLRDHUP |                // for peer close
                   (read ? EPOLLIN : 0) |      // enable read
                   (write ? EPOLLOUT : 0) |    // enable write
                   (edgeTriger ? EPOLLET : 0); // use edge triger or level triger

#ifdef ENABLE_DETAIL_LOGS
    spdlog::debug("[Service::epollAddEndpoint] soc[{}] events[EPOLLRDHUP{}{}{}]",
                  pe->soc,
                  event.events & EPOLLIN ? "|EPOLLIN" : "",
                  event.events & EPOLLOUT ? "|EPOLLOUT" : "",
                  event.events & EPOLLET ? "|EPOLLET" : "");
#endif // ENABLE_DETAIL_LOGS

    if (epoll_ctl(epollfd, EPOLL_CTL_ADD, pe->soc, &event))
    {
        spdlog::error("[Service::epollAddEndpoint] events[EPOLLRDHUP{}{}{}]-soc[{}] join fail. Error {}: {}",
                      event.events & EPOLLIN ? "|EPOLLIN" : "",
                      event.events & EPOLLOUT ? "|EPOLLOUT" : "",
                      event.events & EPOLLET ? "|EPOLLET" : "",
                      event.events, pe->soc, errno, strerror(errno));
        return false;
    }

    return true;
}

bool Service::epollResetEndpointMode(int epollfd, Endpoint_t *pe, bool read, bool write, bool edgeTriger)
{
    struct epoll_event event;
    event.data.ptr = pe;
    event.events = EPOLLRDHUP |                // for peer close
                   (read ? EPOLLIN : 0) |      // enable read
                   (write ? EPOLLOUT : 0) |    // enable write
                   (edgeTriger ? EPOLLET : 0); // use edge triger or level triger

#ifdef ENABLE_DETAIL_LOGS
    spdlog::debug("[Service::epollResetEndpointMode] soc[{}] events[EPOLLRDHUP{}{}{}]",
                  pe->soc,
                  event.events & EPOLLIN ? "|EPOLLIN" : "",
                  event.events & EPOLLOUT ? "|EPOLLOUT" : "",
                  event.events & EPOLLET ? "|EPOLLET" : "");
#endif // ENABLE_DETAIL_LOGS

    if (epoll_ctl(epollfd, EPOLL_CTL_MOD, pe->soc, &event))
    {
        spdlog::error("[Service::epollResetEndpointMode] events[EPOLLRDHUP{}{}{}]-soc[{}] reset fail. Error {}: {}",
                      event.events & EPOLLIN ? "|EPOLLIN" : "",
                      event.events & EPOLLOUT ? "|EPOLLOUT" : "",
                      event.events & EPOLLET ? "|EPOLLET" : "",
                      pe->soc, errno, strerror(errno));
        return false;
    }

    return true;
}

bool Service::epollResetEndpointMode(int epollfd, Tunnel_t *pt, bool read, bool write, bool edgeTriger)
{
    return epollResetEndpointMode(epollfd, pt->north, read, write, edgeTriger) &&
           epollResetEndpointMode(epollfd, pt->south, read, write, edgeTriger);
}

void Service::epollRemoveEndpoint(int epollfd, Endpoint_t *pe)
{
#ifdef ENABLE_DETAIL_LOGS
    spdlog::debug("[Service::epollRemoveEndpoint] soc[{}]", pe->soc);
#endif // ENABLE_DETAIL_LOGS

    // remove from epoll driver
    if (epoll_ctl(epollfd, EPOLL_CTL_DEL, pe->soc, nullptr))
    {
        spdlog::error("[Service::epollRemoveEndpoint] remove endpoint[{}] from epoll fail. {} - {}",
                      Utils::dumpEndpoint(pe), errno, strerror(errno));
    }
}

void Service::epollRemoveTunnel(int epollfd, Tunnel_t *pt)
{
    epollRemoveEndpoint(epollfd, pt->north);
    epollRemoveEndpoint(epollfd, pt->south);
}

} // namespace link
} // namespace mapper
