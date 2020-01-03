#include "utils.h"
#include <assert.h>
#include <ifaddrs.h>
#include <arpa/inet.h>
#include <sstream>
#include <spdlog/spdlog.h>

using namespace std;

namespace mapper
{
namespace link
{

bool Utils::getIntfAddr(const char *intf, sockaddr_in &sa)
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
                spdlog::debug("[Utils::getIntfAddr] IPv4 address of interface[{}]: {}", intf, ip);
                return true;
            }
            else
            {

                spdlog::error("[Utils::getIntfAddr] get IPv4 address of interface[{}] fail", intf);
                return false;
            }
        }

        spdlog::error("[Utils::getIntfAddr] IPv4 address of interface[{}] not found", intf);
        return false;
    }();

    freeifaddrs(ifaddr);

    return result;
}

bool Utils::getAddrInfo(const char *host,
                        const char *service,
                        const Protocol_t protocol,
                        struct addrinfo **ppAddrInfo)
{
    addrinfo hints;

    // init hints
    memset(&hints, 0, sizeof(struct addrinfo));
    hints.ai_family = AF_INET;
    hints.ai_flags = 0;
    hints.ai_canonname = nullptr;
    hints.ai_addr = nullptr;
    hints.ai_next = nullptr;
    if (protocol == Protocol_t::TCP)
    {
        hints.ai_socktype = SOCK_STREAM;
        hints.ai_protocol = IPPROTO_TCP;
    }
    else
    {
        hints.ai_socktype = SOCK_DGRAM;
        hints.ai_protocol = IPPROTO_UDP;
    }

    int nRet = getaddrinfo(host, service, &hints, ppAddrInfo);
    if (nRet != 0)
    {
        spdlog::error("[Utils::getAddrInfo] getaddrinfo fail: {}", gai_strerror(nRet));
        return false;
    }

    return true;
}

void Utils::closeAddrInfo(struct addrinfo *pAddrInfo)
{
    freeaddrinfo(pAddrInfo);
}

int Utils::createServiceSoc(Protocol_t protocol, sockaddr_in *sa, socklen_t salen)
{
    // create socket
    int soc = createSoc(protocol, true);
    if (soc <= 0)
    {
        spdlog::error("[Utils::createServiceSoc] create socket fail. {} - {}", errno, strerror(errno));
        return -1;
    }
    if ([&protocol, &sa, &salen, &soc]() -> bool {
            // set reuse
            int opt = 1;
            if (setsockopt(soc, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &opt, sizeof(opt)))
            {
                spdlog::error("[Utils::createServiceSoc] set reuse fail. {} - {}", errno, strerror(errno));
                return false;
            }
            // bind
            if (bind(soc, (struct sockaddr *)sa, salen))
            {
                spdlog::error("[Utils::createServiceSoc] bind fail. {} - {}", errno, strerror(errno));
                return false;
            }
            // listen
            if (protocol == Protocol_t::TCP && listen(soc, SOMAXCONN << 1))
            {
                spdlog::error("[Utils::createServiceSoc] listen fail. {} - {}", errno, strerror(errno));
                return false;
            }
            return true;
        }())
    {
        return soc;
    }
    else
    {
        close(soc);
        return -1;
    }
}

int Utils::createSoc(Protocol_t protocol, bool nonblock)
{
    // create socket
    int soc = socket(AF_INET,
                     protocol == Protocol_t::TCP ? SOCK_STREAM : SOCK_DGRAM,
                     0);
    if (soc <= 0)
    {
        spdlog::error("[Utils::createServiceSoc] create socket fail. {} - {}", errno, strerror(errno));
        return -1;
    }

    // set to non-block
    if (nonblock)
    {
        int flags = fcntl(soc, F_GETFL);
        if (flags < 0 || fcntl(soc, F_SETFL, flags | O_NONBLOCK) < 0)
        {
            spdlog::error("[Utils::createServiceSoc] set to non-block fail. {} - {}", errno, strerror(errno));
            close(soc);
            return -1;
        }
    }

    return soc;
}

int Utils::compareAddr(const sockaddr *l, const sockaddr *r)
{
    // family
    if (l->sa_family != r->sa_family)
    {
        return l->sa_family < r->sa_family ? -1 : 1;
    }

    // ipv4
    if (l->sa_family == AF_INET)
    {
        auto l_addr = reinterpret_cast<const sockaddr_in *>(l);
        auto r_addr = reinterpret_cast<const sockaddr_in *>(r);
        // address
        if (l_addr->sin_addr.s_addr != r_addr->sin_addr.s_addr)
        {
            return ntohl(l_addr->sin_addr.s_addr) < ntohl(r_addr->sin_addr.s_addr) ? -1 : 1;
        }
        // port
        if (l_addr->sin_port != r_addr->sin_port)
        {
            return ntohs(l_addr->sin_port) < ntohs(r_addr->sin_port) ? -1 : 1;
        }
    }
    else if (l->sa_family == AF_INET6)
    {
        auto l_addr = reinterpret_cast<const sockaddr_in6 *>(l);
        auto r_addr = reinterpret_cast<const sockaddr_in6 *>(r);
        // address
        int r = memcmp(l_addr->sin6_addr.s6_addr,
                       r_addr->sin6_addr.s6_addr,
                       sizeof(l_addr->sin6_addr.s6_addr));
        if (r)
        {
            return r;
        }
        // port
        if (l_addr->sin6_port != r_addr->sin6_port)
        {
            return ntohs(l_addr->sin6_port) < ntohs(r_addr->sin6_port) ? -1 : 1;
        }
        // flowinfo
        if (l_addr->sin6_flowinfo != r_addr->sin6_flowinfo)
        {
            return ntohl(l_addr->sin6_flowinfo) < ntohl(r_addr->sin6_flowinfo) ? -1 : 1;
        }
        // scop id
        if (l_addr->sin6_scope_id != r_addr->sin6_scope_id)
        {
            return ntohl(l_addr->sin6_scope_id) < ntohl(r_addr->sin6_scope_id) ? -1 : 1;
        }
    }
    else
    {
        assert(!"unknown sa_family");
    }

    return 0;
}

int Utils::compareAddr(const sockaddr_in *l, const sockaddr_in *r)
{
    return compareAddr((const sockaddr *)l, (const sockaddr *)r);
}

std::string Utils::dumpSockAddr(const sockaddr_in *addr)
{
    assert(addr);
    return dumpSockAddr(*addr);
}

std::string Utils::dumpSockAddr(const sockaddr_in &addr)
{
    // only support ipv4
    assert(addr.sin_family == AF_INET || addr.sin_family == 0);

    char buffer[32];
    snprintf(buffer, 32, "%d.%d.%d.%d:%d",
             addr.sin_addr.s_addr & 0xFF,
             (addr.sin_addr.s_addr >> 8) & 0xFF,
             (addr.sin_addr.s_addr >> 16) & 0xFF,
             (addr.sin_addr.s_addr >> 24) & 0xFF,
             ntohs(addr.sin_port));
    return buffer;
}

std::string Utils::dumpIpTuple(const IpTuple_t *tuple, bool reverse)
{
    assert(tuple);

    const sockaddr_in *first = &tuple->l;
    const sockaddr_in *second = &tuple->r;
    if (reverse)
    {
        first = second;
        second = &tuple->l;
    }

    stringstream ss;

    ss << dumpSockAddr(first)
       << (tuple->p == Protocol_t::TCP ? "-tcp-" : "-udp-")
       << dumpSockAddr(second);

    return ss.str();
}

std::string Utils::dumpIpTuple(const IpTuple_t &tuple, bool reverse)
{
    return dumpIpTuple(&tuple, reverse);
}

std::string Utils::dumpEndpoint(const Endpoint_t *endpoint, bool reverse)
{
    stringstream ss;

    if (reverse)
    {
        ss << "("
           << dumpIpTuple(endpoint->ipTuple, reverse)
           << ",soc["
           << endpoint->soc
           << "])";
    }
    else
    {
        ss << "(soc["
           << endpoint->soc
           << "],"
           << dumpIpTuple(endpoint->ipTuple, reverse)
           << ")";
    }

    return ss.str();
}

std::string Utils::dumpEndpoint(const Endpoint_t &endpoint, bool reverse)
{
    return dumpIpTuple(endpoint.ipTuple, reverse);
}

std::string Utils::dumpServiceEndpoint(const Endpoint_t *serviceEndpoint, const sockaddr_in *clientAddr)
{
    stringstream ss;

    ss << "("
       << dumpSockAddr(clientAddr)
       << (serviceEndpoint->ipTuple.p == Protocol_t::TCP ? "-tcp-" : "-udp-")
       << dumpSockAddr(serviceEndpoint->ipTuple.l)
       << ",soc["
       << serviceEndpoint->soc
       << "])";

    return ss.str();
}

std::string Utils::dumpServiceEndpoint(const Endpoint_t &serviceEndpoint, const sockaddr_in &clientAddr)
{
    return dumpServiceEndpoint(&serviceEndpoint, &clientAddr);
}

} // namespace link
} // namespace mapper
