#ifndef __MAPPER_ENDPOINT_SERVER_H__
#define __MAPPER_ENDPOINT_SERVER_H__

#include <ifaddrs.h>
#include <netinet/in.h>
#include <string>
#include "base.h"

namespace mapper
{
namespace endpoint
{

class Service : public Base
{
protected:
    Service(int soc) : Base(Type_t::SERVICE, soc) {}

public:
    /**
     * @brief 
     * 
     * @param serviceInfo [interface]:[service port]:[target host]:[target port]
     *      [interface]:     'any' or interface name(for example: eth0). default(empty) is 'any'.
     *      [service port]:  listen port
     *      [target host]:   ip or domain name of target host
     *      [target port]:   target host service port
     * @return Service* new created Service Endpoint Object
     */
    static Service *create(std::string &serviceInfo);
    static Service *create(std::string &interface, int servicePort,
                           std::string &targetHost, int targetPort);

    std::string toStr() override;

    std::string mInterface;
    std::string mIp;
    int mServicePort;
    std::string mTargetHost;
    int mTargetPort;

protected:
    static bool getIfSockAddr(std::string &interface, std::string &ip, sockaddr_in &sa);
};

} // namespace endpoint
} // namespace mapper

#endif // __MAPPER_ENDPOINT_SERVER_H__
