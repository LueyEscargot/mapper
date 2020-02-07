#include "utils.h"
#include <assert.h>
#include <ifaddrs.h>
#include <string.h>
#include <sstream>
#include <spdlog/spdlog.h>

using namespace std;

namespace mapper
{
namespace link
{

const char *Utils::UNITS = " kmgtp";
const uint32_t Utils::UNIT_COUNT = 6;
const uint32_t Utils::UNIT_NUMBER = 1024;

Protocol_t Utils::parseProtocol(const char *protocol)
{
    return strcasecmp(protocol, "tcp") == 0
               ? link::PROTOCOL_TCP
               : strcasecmp(protocol, "udp") == 0
                     ? link::PROTOCOL_UDP
                     : (assert(!"unsupported protocol"),
                        link::PROTOCOL_UNKNOWN);
}

Protocol_t Utils::parseProtocol(const std::string &protocol)
{
    return parseProtocol(protocol.c_str());
}

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
            else
            {
                memcpy(&sa, ifa->ifa_addr, sizeof(sockaddr_in));
                return true;
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
                        addrinfo **ppAddrInfo)
{
    addrinfo hints;

    // init hints
    memset(&hints, 0, sizeof(addrinfo));
    hints.ai_family = AF_INET;
    hints.ai_flags = 0;
    hints.ai_canonname = nullptr;
    hints.ai_addr = nullptr;
    hints.ai_next = nullptr;
    if (protocol == PROTOCOL_TCP)
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

void Utils::closeAddrInfo(addrinfo *pAddrInfo)
{
    freeaddrinfo(pAddrInfo);
}

int Utils::createSoc(Protocol_t protocol, bool nonblock)
{
    // create socket
    int soc = socket(AF_INET,
                     protocol == PROTOCOL_TCP ? SOCK_STREAM : SOCK_DGRAM,
                     0);
    if (soc <= 0)
    {
        spdlog::error("[Utils::createSoc] create socket fail. {} - {}", errno, strerror(errno));
        return -1;
    }

    // set to non-block
    if (!setSocAttr(soc, true, false))
    {
        spdlog::error("[Utils::createSoc] set to attr fail.");
        close(soc);
        return -1;
    }

    // reset receive buffer size for UDP socket
    if (protocol == PROTOCOL_UDP)
    {
        int s = 1 << 20; // 1MB
        if (setsockopt(soc, SOL_SOCKET, SO_RCVBUF, (const char *)&s, sizeof(s)))
        {
            spdlog::error("[Utils::createSoc] reset udp receive buffer fail. {} - {}",
                          errno, strerror(errno));
            return false;
        }
    }

    return soc;
}

int Utils::createServiceSoc(Protocol_t protocol, sockaddr_in *sa, socklen_t salen)
{
    // create socket
    int soc = socket(AF_INET, protocol == PROTOCOL_TCP ? SOCK_STREAM : SOCK_DGRAM, 0);
    if (soc <= 0)
    {
        spdlog::error("[Utils::createServiceSoc] create socket fail. {} - {}", errno, strerror(errno));
        return -1;
    }

    if ([&protocol, &sa, &salen, &soc]() -> bool {
            if (!setSocAttr(soc, true, true))
            {
                spdlog::error("[Utils::createServiceSoc] set soc attr fail.");
                return false;
            }
            // bind
            if (bind(soc, (sockaddr *)sa, salen))
            {
                spdlog::error("[Utils::createServiceSoc] bind fail. {} - {}", errno, strerror(errno));
                return false;
            }
            switch (protocol)
            {
            case PROTOCOL_TCP:
                // listen
                if (listen(soc, SOMAXCONN << 1))
                {
                    spdlog::error("[Utils::createServiceSoc] listen fail. {} - {}", errno, strerror(errno));
                    return false;
                }
                break;
            case PROTOCOL_UDP:
            {
                // reset receive buffer size for udp service socket
                int s = 1 << 24; // 16MB
                if (setsockopt(soc, SOL_SOCKET, SO_RCVBUF, &s, sizeof(s)))
                {
                    spdlog::error("[Utils::createServiceSoc] reset udp receive buffer size fail. {} - {}",
                                  errno, strerror(errno));
                    return false;
                }
            }
            break;

            default:
                assert(!"unsupported protocol");
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

bool Utils::setSocAttr(int soc, bool nonblock, bool reuse)
{
    // set to non-block
    if (nonblock)
    {
        int flags = fcntl(soc, F_GETFL);
        if (flags < 0 || fcntl(soc, F_SETFL, flags | O_NONBLOCK) < 0)
        {
            spdlog::error("[Utils::setSocAttr] set to non-block fail. {} - {}",
                          errno, strerror(errno));
            close(soc);
            return false;
        }
    }
    // set reuse
    if (reuse)
    {
        int opt = 1;
        if (setsockopt(soc, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &opt, sizeof(opt)))
        {
            spdlog::error("[Utils::setSocAttr] set reuse fail. {} - {}",
                          errno, strerror(errno));
            return false;
        }
    }

    return true;
}

int Utils::compareAddr(const sockaddr *l, const sockaddr *r)
{
    return (l->sa_family != r->sa_family) // family
               ? (l->sa_family < r->sa_family
                      ? -1
                      : 1)
               : (l->sa_family == AF_INET) // ipv4
                     ? compareAddr((const sockaddr_in *)l, (const sockaddr_in *)r)
                     : (l->sa_family == AF_INET6) // ipv6
                           ? compareAddr((const sockaddr_in6 *)l, (const sockaddr_in6 *)r)
                           : (assert(!"unknown sa_family"), 0);
}

int Utils::compareAddr(const sockaddr_in *l, const sockaddr_in *r)
{
    return (l->sin_addr.s_addr != r->sin_addr.s_addr)
               ? (ntohl(l->sin_addr.s_addr) < ntohl(r->sin_addr.s_addr)
                      ? -1
                      : 1)
               : (l->sin_port != r->sin_port)
                     ? (ntohs(l->sin_port) < ntohs(r->sin_port)
                            ? -1
                            : 1)
                     : 0;
}

int Utils::compareAddr(const sockaddr_in6 *l, const sockaddr_in6 *r)
{
    // address
    int addrCmpRet = memcmp(l->sin6_addr.s6_addr,
                            r->sin6_addr.s6_addr,
                            sizeof(l->sin6_addr.s6_addr));

    return addrCmpRet
               ? addrCmpRet
               : (l->sin6_port != r->sin6_port)
                     ? (ntohs(l->sin6_port) < ntohs(r->sin6_port)
                            ? -1
                            : 1)
                     : (l->sin6_flowinfo != r->sin6_flowinfo)
                           ? (ntohl(l->sin6_flowinfo) < ntohl(r->sin6_flowinfo)
                                  ? -1
                                  : 1)
                           : (l->sin6_scope_id != r->sin6_scope_id)
                                 ? (ntohl(l->sin6_scope_id) < ntohl(r->sin6_scope_id)
                                        ? -1
                                        : 1)
                                 : 0;
}

int Utils::compareAddr(const addrinfo *l, const addrinfo *r)
{
#define __MLCOMP__(Name)                   \
    if (l->Name != r->Name)                \
    {                                      \
        return l->Name < r->Name ? -1 : 1; \
    }

    __MLCOMP__(ai_flags);                             // Input flags.
    __MLCOMP__(ai_family);                            // Protocol family for socket.
    __MLCOMP__(ai_socktype);                          // Socket type.
    __MLCOMP__(ai_protocol);                          // Protocol for socket.
    __MLCOMP__(ai_addrlen);                           // Length of socket address.
    if (auto n = compareAddr(l->ai_addr, r->ai_addr)) // Socket address for socket.
    {
        return n;
    }
    if (auto n = strcmp(l->ai_canonname, r->ai_canonname)) // Canonical name for service location.
    {
        return n;
    }

    return 0;
}

std::string Utils::dumpSockAddr(const sockaddr *addr)
{
    return dumpSockAddr((const sockaddr_in *)addr);
}

std::string Utils::dumpSockAddr(const sockaddr &addr)
{
    return dumpSockAddr(&addr);
}

std::string Utils::dumpSockAddr(const sockaddr_in *addr)
{
    assert(addr);
    // only support ipv4
    assert(addr->sin_family == AF_INET || addr->sin_family == 0);

    char buffer[32];
    snprintf(buffer, 32, "%d.%d.%d.%d:%d",
             addr->sin_addr.s_addr & 0xFF,
             (addr->sin_addr.s_addr >> 8) & 0xFF,
             (addr->sin_addr.s_addr >> 16) & 0xFF,
             (addr->sin_addr.s_addr >> 24) & 0xFF,
             ntohs(addr->sin_port));

    return buffer;
}

std::string Utils::dumpSockAddr(const sockaddr_in &addr)
{
    return dumpSockAddr(&addr);
}

std::string Utils::dumpConnection(const Connection_t *conn, bool reverse)
{
    assert(conn);

    const sockaddr_in *first = &conn->localAddr;
    const sockaddr_in *second = &conn->remoteAddr;
    if (reverse)
    {
        first = second;
        second = &conn->localAddr;
    }

    stringstream ss;

    ss << dumpSockAddr(first)
       << (conn->protocol == PROTOCOL_TCP ? "-tcp-" : "-udp-")
       << dumpSockAddr(second);

    return ss.str();
}

std::string Utils::dumpConnection(const Connection_t &conn, bool reverse)
{
    return dumpConnection(&conn, reverse);
}

std::string Utils::dumpEndpoint(const Endpoint_t *endpoint, bool reverse)
{
    stringstream ss;

    if (reverse)
    {
        ss << "("
           << dumpConnection(endpoint->conn, reverse)
           << ",soc["
           << endpoint->soc
           << "])";
    }
    else
    {
        ss << "(soc["
           << endpoint->soc
           << "],"
           << dumpConnection(endpoint->conn, reverse)
           << ")";
    }

    return ss.str();
}

std::string Utils::dumpEndpoint(const Endpoint_t &endpoint, bool reverse)
{
    return dumpConnection(endpoint.conn, reverse);
}

std::string Utils::dumpServiceEndpoint(const Endpoint_t *serviceEndpoint, const sockaddr_in *clientAddr)
{
    stringstream ss;

    ss << "("
       << dumpSockAddr(clientAddr)
       << (serviceEndpoint->conn.protocol == PROTOCOL_TCP ? "-tcp-" : "-udp-")
       << dumpSockAddr(serviceEndpoint->conn.localAddr)
       << ",soc["
       << serviceEndpoint->soc
       << "])";

    return ss.str();
}

std::string Utils::dumpServiceEndpoint(const Endpoint_t &serviceEndpoint, const sockaddr_in &clientAddr)
{
    return dumpServiceEndpoint(&serviceEndpoint, &clientAddr);
}

std::string Utils::dumpTunnel(const Tunnel_t *pt, bool reverse)
{
    stringstream ss;

    ss << "("
       << dumpEndpoint(pt->south, true)
       << "==>"
       << dumpEndpoint(pt->north, false)
       << ")";

    return ss.str();
}

std::string Utils::dumpTunnel(const Tunnel_t &pt, bool reverse)
{
    return dumpTunnel(&pt, reverse);
}

std::string Utils::toHumanStr(float num)
{
    int i;
    for (i = 0; i < UNIT_COUNT - 1; ++i)
    {
        if (num < 1000)
        {
            break;
        }

        num /= 1000;
    }

    stringstream ss;

    ss << num
       << UNITS[i];

    ss.precision(3);

    return ss.str();
}

} // namespace link
} // namespace mapper
