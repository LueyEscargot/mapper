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
    static bool getIntfAddr(const char *intf, sockaddr_in &sa);
    static bool getAddrInfo(const char *host,
                            const char *service,
                            const Protocol_t protocol,
                            struct addrinfo **ppAddrInfo);
    static void closeAddrInfo(struct addrinfo *pAddrInfo);
    static int createSoc(Protocol_t protocol, bool nonblock);
    static int createServiceSoc(Protocol_t protocol, sockaddr_in *sa, socklen_t salen);

    static int compareAddr(const sockaddr *l, const sockaddr *r);
    static int compareAddr(const sockaddr_in *l, const sockaddr_in *r);
};

} // namespace link
} // namespace mapper

#endif // __MAPPER_LINK_UTILS_H__
