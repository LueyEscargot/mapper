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

EndpointService_t *Endpoint::createService(const char *strProtocol,
                                           const char *intf,
                                           const char *service,
                                           const char *targetHost,
                                           const char *targetService)
{
    Protocol_t protocol = strcasecmp(strProtocol, "tcp") == 0 ? Protocol_t::TCP : Protocol_t::UDP;

    // get local address of specified interface
    sockaddr_in sa;
    if (!Endpoint::getIntfAddr(intf, sa))
    {
        spdlog::error("[Endpoint::createService] get local address of specified interface[{}] fail", intf);
        return nullptr;
    }
    sa.sin_port = htons(atoi(service));

    // create non-block server socket
    int soc = createTcpServiceSoc(sa);
    if (soc < 0)
    {
        spdlog::error("[Endpoint::createService] create service soc[{}:{}] fail: {} - {}",
                      intf, service, errno, strerror(errno));
        return nullptr;
    }

    // create service endpoint
    EndpointService_t *pes = new EndpointService_t;
    if (pes == nullptr)
    {
        spdlog::error("[Endpoint::createService] create service endpoint for interface[{}] fail", intf);
        close(soc);
        return nullptr;
    }
    else
    {
        spdlog::debug("[Endpoint::createService] create endpoint for intf[{}]", intf);
        pes->init(soc, protocol, intf, service, targetHost, targetService);
    }

    // get target addr
    if (!getAddrInfo(pes))
    {
        spdlog::error("[Endpoint::createService] get addr of host[{}] fail", pes->targetHost);
        close(soc);
        delete pes;
        return nullptr;
    }

    return pes;
}

void Endpoint::releaseService(EndpointService_t *pService)
{
    if (pService)
    {
        // free addr info
        assert(pService->targetHostAddrs);
        freeaddrinfo(pService->targetHostAddrs);

        delete pService;
    }
}

string Endpoint::toStr(const EndpointBase_t *pEndpoint)
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

string Endpoint::toStr(const EndpointService_t *pEndpoint)
{
    stringstream ss;

    ss << "["
       << toStr(static_cast<const EndpointBase_t *>(pEndpoint)) << ","
       << (pEndpoint->protocol == Protocol_t::TCP
               ? "tcp"
               : (pEndpoint->protocol == Protocol_t::UDP
                      ? "udp"
                      : (assert(false), "N/A")))
       << ","
       << pEndpoint->interface << ","
       << pEndpoint->service << ","
       << pEndpoint->targetHost << ","
       << pEndpoint->targetService
       << "]";

    return ss.str();
}

string Endpoint::toStr(const EndpointRemote_t *pEndpoint)
{
    stringstream ss;

    ss << "["
       << toStr(static_cast<const EndpointBase_t *>(pEndpoint)) << ","
       << pEndpoint->tunnel
       << "]";

    return ss.str();
}

string Endpoint::toStr(const Tunnel_t *pTunnel)
{
    stringstream ss;

    ss << "[["
       << toStr(static_cast<const EndpointBase_t *>(&pTunnel->south)) << ","
       << pTunnel->south.tunnel
       << "],["
       << toStr(static_cast<const EndpointBase_t *>(&pTunnel->north)) << ","
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

bool Endpoint::getAddrInfo(EndpointService_t *pes)
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

    int nRet = getaddrinfo(pes->targetHost, pes->targetService, &hints, &pes->targetHostAddrs);
    if (nRet != 0)
    {
        spdlog::error("[Endpoint::getAddrInfo] getaddrinfo fail: {}", gai_strerror(nRet));
        return false;
    }
}

int Endpoint::createTcpServiceSoc(sockaddr_in &sa)
{
    auto logErr = [](const char *title, bool b) -> bool {
        if (b)
        {
            spdlog::error("[Endpoint::createService] {}: {} - {}", title, errno, strerror(errno));
            return b;
        }
    };

    int soc = 0;
    int flags = 0;
    int opt = 1;

    if (
        // create socket
        logErr("create socket fail", soc = socket(AF_INET, SOCK_STREAM, 0) < 0) ||
        // set to non-block
        logErr("fcntl fail", flags = fcntl(soc, F_GETFL) == -1) ||
        logErr("set to non-block fail", fcntl(soc, F_SETFL, flags | O_NONBLOCK) == -1) ||
        // set reuse
        logErr("set reuse fail", setsockopt(soc, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &opt, sizeof(opt))) ||
        // bind
        logErr("bind fail", bind(soc, (struct sockaddr *)&sa, sizeof(sa))) ||
        // listen
        logErr("listen fail", listen(soc, SOMAXCONN) == -1))
    {
        if (soc > 0)
        {
            close(soc);
        }
        return -1;
    }

    return soc;
}

} // namespace link
} // namespace mapper
