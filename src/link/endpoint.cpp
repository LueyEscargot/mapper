#include "endpoint.h"
#include <assert.h>
#include <fcntl.h>
#include <ifaddrs.h>
#include <netdb.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <sstream>
#include <spdlog/spdlog.h>

using namespace std;

namespace mapper
{
namespace link
{

regex Endpoint::REG_FORWARD_SETTING_STRING = regex(R"(^\s*\[(\w+):(\w+):(\d+):([a-zA-Z0-9-_.]+):(\d+)]\s*$)");

EndpointService_t *Endpoint::createService(string forward)
{
    smatch m;
    if (regex_match(forward, m, REG_FORWARD_SETTING_STRING))
    {
        assert(m.size() == 6);     // section
        string protocol = m[1];    // protocol
        string interface = m[2];   // interface
        string servicePort = m[3]; // service port
        string targetHost = m[4];  // target host
        string targetPort = m[5];  // target port

        return createService(protocol.c_str(), interface.c_str(), servicePort.c_str(), targetHost.c_str(), targetPort.c_str());
    }
    else
    {
        spdlog::error("[Endpoint::createService] invalid forward setting format: {}", forward);
        return nullptr;
    }
}

EndpointService_t *Endpoint::createService(const char *strProtocol,
                                           const char *intf,
                                           const char *servicePort,
                                           const char *targetHost,
                                           const char *targetPort)
{
    Protocol_t protocol = strcasecmp(strProtocol, "tcp") == 0 ? Protocol_t::TCP : Protocol_t::UDP;

    // get local address of specified interface
    sockaddr_in sa;
    if (!Endpoint::getIntfAddr(intf, sa))
    {
        spdlog::error("[Endpoint::createService] get local address of specified interface[{}] fail", intf);
        return nullptr;
    }

    // create non-block server socket
    int soc = socket(AF_INET,
                     protocol == Protocol_t::TCP ? SOCK_STREAM : SOCK_DGRAM,
                     0);
    if (soc < 0)
    {
        spdlog::error("[Endpoint::createService] create socket fail: {} - {}", errno, strerror(errno));
        return nullptr;
    }
    if ([&]() -> bool {
            // set to non-block
            int flags = fcntl(soc, F_GETFL);
            if (flags == -1 || fcntl(soc, F_SETFL, flags | O_NONBLOCK) == -1)
            {
                spdlog::error("[Endpoint::createService] set to non-block fail: {} - {}", errno, strerror(errno));
                return false;
            }

            // set reuse
            int opt = 1;
            if (setsockopt(soc, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &opt, sizeof(opt)))
            {
                spdlog::error("[Endpoint::createService] set reuse fail: {} - {}", errno, strerror(errno));
                return false;
            }

            // bind
            sa.sin_port = htons(atoi(servicePort));
            if (bind(soc, (struct sockaddr *)&sa, sizeof(sa)))
            {
                spdlog::error("[Endpoint::createService] bind to intf[{}] fail: {} - {}",
                              intf, errno, strerror(errno));
                return false;
            }

            // listen
            if (protocol == Protocol_t::TCP && listen(soc, SOMAXCONN) == -1)
            {
                spdlog::error("[Endpoint::createService] Listen at intf[{}] fail: {} - {}",
                              intf, errno, strerror(errno));
                return false;
            }

            return true;
        }())
    {
        if (EndpointService_t *pes = new EndpointService_t)
        {
            spdlog::debug("[Endpoint::createService] create endpoint for intf[{}]", intf);
            pes->init(soc, protocol, intf, servicePort, targetHost, targetPort);
            return pes;
        }
        else
        {
            spdlog::error("[Endpoint::createService] create service endpoint for interface[{}] fail", intf);
            close(soc);
            return nullptr;
        }
    }
    else
    {
        close(soc);
        return nullptr;
    }
}

void Endpoint::releaseService(EndpointService_t *pService)
{
    if (pService)
    {
        if (!dynamic_cast<EndpointService_t *>(pService))
        {
            spdlog::critical("[Endpoint::releaseService] pointer[{}] is NOT EndpointService_t pointer!", static_cast<void *>(pService));
            assert(false);
        }

        delete pService;
    }
}

bool Endpoint::createTunnel(const EndpointService_t *pes, Tunnel_t *pt)
{
    assert(pes && pt);

    if (pes->protocol == Protocol_t::TCP)
    {
        // get addr info
        if (!getAddrInfo(pes, pt) || pt->addrHead == nullptr)
        {
            spdlog::error("[Endpoint::createTunnel] get address of target[{}] fail", pes->targetHost);
            return false;
        }

        // create socket and connect to target host
        if (!connectToTarget(pes, pt))
        {
            spdlog::error("[Endpoint::createTunnel] connect to target[{}] fail", pes->targetHost);
            return false;
        }

        pt->status = TunnelState_t::CONNECT;
    }
    else
    {
        // TODO: implement this function.
        spdlog::error("[Endpoint::createTunnel] UDP service not implimented yet.");
        return false;
    }
}

string Endpoint::toStr(EndpointBase_t *pEndpoint)
{
    stringstream ss;

    ss << "["
       << ((pEndpoint->type == Type_t::SERVICE)
               ? "Service"
               : (pEndpoint->type == Type_t::NORTH)
                     ? "North"
                     : (pEndpoint->type == Type_t::SOUTH)
                           ? "South"
                           : (spdlog::critical("[Endpoint::toStr] invalid endpoint type: [{}]", pEndpoint->type),
                              assert(false),
                              "N/A"))
       << ","
       << pEndpoint->soc << ","
       << pEndpoint->valid
       << "]";

    return ss.str();
}

string Endpoint::toStr(EndpointService_t *pEndpoint)
{
    stringstream ss;

    ss << "["
       << toStr(static_cast<EndpointBase_t *>(pEndpoint)) << ","
       << (pEndpoint->protocol == Protocol_t::TCP
               ? "tcp"
               : (pEndpoint->protocol == Protocol_t::UDP
                      ? "udp"
                      : (assert(false), "N/A")))
       << ","
       << pEndpoint->interface << ","
       << pEndpoint->servicePort << ","
       << pEndpoint->targetHost << ","
       << pEndpoint->targetPort
       << "]";

    return ss.str();
}

string Endpoint::toStr(EndpointRemote_t *pEndpoint)
{
    stringstream ss;

    ss << "["
       << toStr(static_cast<EndpointBase_t *>(pEndpoint)) << ","
       << pEndpoint->tunnel
       << "]";

    return ss.str();
}

string Endpoint::toStr(Tunnel_t *pTunnel)
{
    stringstream ss;

    ss << "[["
       << toStr(static_cast<EndpointBase_t *>(&pTunnel->south)) << ","
       << pTunnel->south.tunnel
       << "],["
       << toStr(static_cast<EndpointBase_t *>(&pTunnel->north)) << ","
       << pTunnel->north.tunnel
       << "],"
       << pTunnel->status << ","
       << pTunnel->tag
       << "]";

    return ss.str();
}

bool Endpoint::getIntfAddr(const char *intf, sockaddr_in &sa)
{
    if (strcasecmp(intf, "any") == 0)
    {
        memset(&sa, 0, sizeof(sa));

        sa.sin_family = AF_INET;
        sa.sin_addr.s_addr = htonl(INADDR_ANY);
        return true;
    }

    struct ifaddrs *ifaddr;
    if (getifaddrs(&ifaddr))
    {
        return false;
    }

    bool result = [&ifaddr, &intf, &sa]() -> bool {
        for (auto ifa = ifaddr; ifa != nullptr; ifa = ifa->ifa_next)
        {
            if (ifa->ifa_addr == nullptr ||
                ifa->ifa_addr->sa_family != AF_INET ||
                strcasecmp(ifa->ifa_name, intf))
            {
                continue;
            }

            memcpy(&sa, ifa->ifa_addr, sizeof(sockaddr_in));

            char ip[INET_ADDRSTRLEN];
            if (inet_ntop(AF_INET, &sa, ip, INET_ADDRSTRLEN))
            {
                spdlog::debug("[Endpoint::getIntfAddr] IPv4 address of interface[{}]: {}", intf, ip);
                return true;
            }
            else
            {

                spdlog::error("[Endpoint::getIntfAddr] get IPv4 address of interface[{}] fail", intf);
                return false;
            }
        }

        spdlog::error("[Endpoint::getIntfAddr] IPv4 address of interface[{}] not found", intf);
        return false;
    }();

    freeifaddrs(ifaddr);

    return result;
}

bool Endpoint::getAddrInfo(const EndpointService_t *pes, Tunnel_t *pt)
{
    addrinfo hints;

    // init hints
    memset(&hints, 0, sizeof(struct addrinfo));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = 0;
    hints.ai_protocol = IPPROTO_TCP;
    hints.ai_canonname = nullptr;
    hints.ai_addr = nullptr;
    hints.ai_next = nullptr;

    int nRet = getaddrinfo(pes->targetHost, pes->targetPort, &hints, &pt->addrHead);
    if (nRet != 0)
    {
        spdlog::error("[Endpoint::getAddrInfo] getaddrinfo fail: {}", gai_strerror(nRet));
        return false;
    }
}

bool Endpoint::connectToTarget(const EndpointService_t *pes, Tunnel_t *pt)
{
    if ((pt->north.soc = socket(pt->curAddr->ai_family,
                                pt->curAddr->ai_socktype,
                                pt->curAddr->ai_protocol)) < 0)
    {
        spdlog::error("[Endpoint::connectToTarget] socket creation error{}: {}",
                      errno, strerror(errno));
        return false;
    }

    // set socket to non-blocking mode
    if (fcntl(pt->north.soc, F_SETFL, O_NONBLOCK) < 0)
    {
        spdlog::error("[Endpoint::connectToTarget] set socket to non-blocking mode fail. {}: {}",
                      errno, strerror(errno));
        close(pt->north.soc);
        pt->north.soc = 0;
        return false;
    }

    char ip[INET_ADDRSTRLEN];

    for (pt->curAddr = pt->addrHead; pt->curAddr; pt->curAddr = pt->curAddr->ai_next)
    {
        inet_ntop(AF_INET, &pt->curAddr->ai_addr, ip, INET_ADDRSTRLEN);
        spdlog::debug("[Endpoint::connectToTarget] connect to {} ({}:{})",
                      pes->targetHost, ip, pes->targetPort);

        // connect to host
        if (connect(pt->north.soc, pt->curAddr->ai_addr, pt->curAddr->ai_addrlen) < 0 &&
            errno != EALREADY && errno != EINPROGRESS)
        {
            if (pt->curAddr->ai_next)
            {
                spdlog::error("[Endpoint::connectToTarget] connect fail: {}, {}, try again",
                              errno, strerror(errno));
            }
            else
            {
                spdlog::error("[Endpoint::connectToTarget] connect fail: {}, {}",
                              errno, strerror(errno));
                close(pt->north.soc);
                pt->north.soc = 0;
                return false;
            }
        }
        else
        {
            return true;
        }
    }
}

} // namespace link
} // namespace mapper