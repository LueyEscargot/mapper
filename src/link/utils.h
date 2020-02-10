/**
 * @file utils.h
 * @author Liu Yu (source@liuyu.com)
 * @brief Utils Class.
 * @version 1
 * @date 2019-12-30
 * 
 * @copyright Copyright (c) 2019
 * 
 */
#ifndef __MAPPER_LINK_UTILS_H__
#define __MAPPER_LINK_UTILS_H__

#include <assert.h>
#include <netdb.h>
#include <sys/socket.h>
#include <string>
#include "type.h"

namespace mapper
{
namespace link
{

class Utils
{
public:
    struct Comparator_t
    {
        // for sockaddr_in
        inline bool operator()(const sockaddr_in &l, const sockaddr_in &r) const
        {
            return Utils::compareAddr(&l, &r) < 0;
        }
        inline bool operator()(const sockaddr &l, const sockaddr &r) const
        {
            assert(l.sa_family == r.sa_family && l.sa_family == AF_INET);
            return Utils::compareAddr((const sockaddr_in *)&l, (const sockaddr_in *)&r) < 0;
        }
    };

    static Protocol_t parseProtocol(const char *protocol);
    static Protocol_t parseProtocol(const std::string &protocol);

    static bool getIntfAddr(const char *intf, sockaddr_in &sa);
    static bool getAddrInfo(const char *host,
                            const char *service,
                            const Protocol_t protocol,
                            addrinfo **ppAddrInfo);
    static void closeAddrInfo(addrinfo *pAddrInfo);
    static int createSoc(Protocol_t protocol, bool nonblock);
    static int createServiceSoc(Protocol_t protocol, sockaddr_in *sa, socklen_t salen);
    static bool setSocAttr(int soc, bool nonblock, bool reuse);

    static int compareAddr(const sockaddr *l, const sockaddr *r);
    static int compareAddr(const sockaddr_in *l, const sockaddr_in *r);
    static int compareAddr(const sockaddr_in6 *l, const sockaddr_in6 *r);
    static int compareAddr(const addrinfo *l, const addrinfo *r);

    static std::string dumpSockAddr(const sockaddr *addr);
    static std::string dumpSockAddr(const sockaddr &addr);
    static std::string dumpSockAddr(const sockaddr_in *addr);
    static std::string dumpSockAddr(const sockaddr_in &addr);
    static std::string dumpConnection(const Connection_t *conn, bool reverse = false);
    static std::string dumpConnection(const Connection_t &conn, bool reverse = false);
    static std::string dumpEndpoint(const Endpoint_t *endpoint, bool reverse = false);
    static std::string dumpEndpoint(const Endpoint_t &endpoint, bool reverse = false);
    static std::string dumpServiceEndpoint(const Endpoint_t *serviceEndpoint, const sockaddr_in *clientAddr);
    static std::string dumpServiceEndpoint(const Endpoint_t &serviceEndpoint, const sockaddr_in &clientAddr);
    static std::string dumpTunnel(const Tunnel_t *Tunnel, bool reverse = false);
    static std::string dumpTunnel(const Tunnel_t &Tunnel, bool reverse = false);

    static std::string toHumanStr(float num);
};

} // namespace link
} // namespace mapper

#endif // __MAPPER_LINK_UTILS_H__
