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

namespace mapper
{
namespace link
{

const string Service::CONFIG_BASE_PATH = "/service";

Service::Service(string name)
    : mName(name)
{
}

bool Service::create(int epollfd, DynamicBuffer *pBuffer, Document &cfg, list<Service *> &serviceList)
{
    auto serviceCfg = JsonUtils::getObj(&cfg, CONFIG_BASE_PATH);
    assert(serviceCfg);

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
                    auto pService = new TcpForwardService;
                    if (pService)
                    {
                        if (pService->init(epollfd, pBuffer, forward, setting))
                        {
                            spdlog::debug("[Service::create] create service: {}", forwardStr);
                            serviceList.push_back(pService);
                        }
                        else
                        {
                            spdlog::error("[Service::create] init tcp forward service object fail");
                            delete pService;
                        }
                    }
                    else
                    {
                        spdlog::error("[Service::create] create tcp forward service object fail");
                    }
                }
                else if (protocol == PROTOCOL_UDP)
                {
                    // UDP Forward service
                    auto pService = new UdpForwardService;
                    if (pService)
                    {
                        if (pService->init(epollfd, pBuffer, forward, setting))
                        {
                            spdlog::debug("[Service::create] create service: {}", forwardStr);
                            serviceList.push_back(pService);
                        }
                        else
                        {
                            spdlog::error("[Service::create] init udp forward service object fail");
                            delete pService;
                        }
                    }
                    else
                    {
                        spdlog::error("[Service::create] create udp forward service fail");
                    }
                }
                else
                {
                    spdlog::error("[Service::create] unsupported protocol[{}]", forward->protocol);
                }
            }
            else
            {
                spdlog::warn("[TcpForwardService::create] skip invalid forward: {}", forwardStr);
            }
        }
    }

    return true;
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
        JsonUtils::getAsUint32(cfg,
                               CONFIG_BASE_PATH + "/setting/buffer/size",
                               SEETING_BUFFER_SIZE) *
        SEETING_BUFFER_SIZE_UNIT;
    setting.bufferPerSessionLimit =
        JsonUtils::getAsUint32(cfg,
                               CONFIG_BASE_PATH + "/setting/buffer/perSessionLimit",
                               SEETING_BUFFER_PERSESSIONLIMIT) *
        SEETING_BUFFER_SIZE_UNIT;
}

string Service::dumpSetting(Setting_t &setting)
{
    stringstream ss;

    ss << R"({"timeout": {"connect":)" << setting.connectTimeout
       << R"(,"session":)" << setting.sessionTimeout
       << R"(,"release":)" << setting.releaseTimeout
       << R"(},"buffer": {"size":)" << setting.bufferSize
       << R"(,"perSessionLimit":)" << setting.bufferPerSessionLimit
       << R"(}})";

    return ss.str();
}

bool Service::init(int epollfd, DynamicBuffer *pBuffer)
{
    mEpollfd = epollfd;
    mpBuffer = pBuffer;

    return true;
}

bool Service::epollAddEndpoint(Endpoint_t *pe, bool read, bool write, bool edgeTriger)
{
    // spdlog::debug("[Service::epollAddEndpoint] endpoint[{}], read[{}], write[{}]",
    //               Endpoint::toStr(pe), read, write);

    struct epoll_event event;
    event.data.ptr = pe;
    event.events = EPOLLRDHUP |                // for peer close
                   (read ? EPOLLIN : 0) |      // enable read
                   (write ? EPOLLOUT : 0) |    // enable write
                   (edgeTriger ? EPOLLET : 0); // use edge triger or level triger
    if (epoll_ctl(mEpollfd, EPOLL_CTL_ADD, pe->soc, &event))
    {
        spdlog::error("[Service::epollAddEndpoint] events[EPOLLRDHUP{}{}{}]-soc[{}] join fail. Error {}: {}",
                      event.events & EPOLLIN ? "|EPOLLIN" : "",
                      event.events & EPOLLOUT ? "|EPOLLOUT" : "",
                      event.events & EPOLLET ? "|EPOLLET" : "",
                      event.events, pe->soc, errno, strerror(errno));
        return false;
    }

    // spdlog::debug("[Service::epollAddEndpoint] endpoint[{}], event.events[0x{:X}]",
    //               Endpoint::toStr(pe), event.events);

    return true;
}

bool Service::epollResetEndpointMode(Endpoint_t *pe, bool read, bool write, bool edgeTriger)
{
    // spdlog::debug("[Service::epollResetEndpointMode] endpoint[{}], read: {}, write: {}, edgeTriger: {}",
    //               Utils::dumpEndpoint(pe), read, write, edgeTriger);

    struct epoll_event event;
    event.data.ptr = pe;
    event.events = EPOLLRDHUP |                // for peer close
                   (read ? EPOLLIN : 0) |      // enable read
                   (write ? EPOLLOUT : 0) |    // enable write
                   (edgeTriger ? EPOLLET : 0); // use edge triger or level triger
    if (epoll_ctl(mEpollfd, EPOLL_CTL_MOD, pe->soc, &event))
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

bool Service::epollResetEndpointMode(Tunnel_t *pt, bool read, bool write, bool edgeTriger)
{
    return epollResetEndpointMode(pt->north, read, write, edgeTriger) &&
           epollResetEndpointMode(pt->south, read, write, edgeTriger);
}

void Service::epollRemoveEndpoint(Endpoint_t *pe)
{
    // spdlog::trace("[Service::epollRemoveEndpoint] remove endpoint[{}]",
    //               Utils::dumpEndpoint(pe));

    // remove from epoll driver
    if (epoll_ctl(mEpollfd, EPOLL_CTL_DEL, pe->soc, nullptr))
    {
        spdlog::error("[Service::epollRemoveEndpoint] remove endpoint[{}] from epoll fail. {} - {}",
                      Utils::dumpEndpoint(pe), errno, strerror(errno));
    }
}

void Service::epollRemoveTunnel(Tunnel_t *pt)
{
    epollRemoveEndpoint(pt->north);
    epollRemoveEndpoint(pt->south);
}

} // namespace link
} // namespace mapper
