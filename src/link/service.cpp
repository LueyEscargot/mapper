#include "service.h"
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sstream>
#include <spdlog/spdlog.h>

// #define _GNU_SOURCE /* To get defns of NI_MAXSERV and NI_MAXHOST */
#include <arpa/inet.h>
#include <ifaddrs.h>

using namespace std;

namespace mapper
{
namespace link
{

Service *Service::create(string &serviceInfo)
{
    // interface
    int begin = 0;
    int end = serviceInfo.find(":");
    string interface = serviceInfo.substr(begin, end);

    // service port
    begin = end + 1;
    end = serviceInfo.find(":", begin);
    string strVal = serviceInfo.substr(begin, end);
    int servicePort = strtoul(strVal.c_str(), nullptr, 10);

    // target host
    begin = end + 1;
    end = serviceInfo.find(":", begin);
    string targetHost = serviceInfo.substr(begin, end);

    // target port
    begin = end + 1;
    strVal = serviceInfo.substr(begin, serviceInfo.length());
    int targetPort = strtoul(strVal.c_str(), nullptr, 10);

    return (interface.empty() ||
            servicePort == 0 || servicePort > 65535 ||
            targetHost.empty() ||
            targetPort == 0 || targetPort > 65535)
               ? nullptr
               : create(interface, servicePort, targetHost, targetPort);
}

Service *Service::create(string &interface, int servicePort,
                         string &targetHost, int targetPort)
{
    // get address of interface
    string ip;
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    if (!getIfSockAddr(interface, ip, addr))
    {
        spdlog::error("[Service::create] get address of interface fail");
        return nullptr;
    }
    // set service port
    addr.sin_port = htons(servicePort);

    // create socket
    int soc = socket(AF_INET, SOCK_STREAM, 0);
    if (soc <= 0)
    {
        spdlog::error("[Service::create] Fail to create service socket. Error{}: {}",
                      errno, strerror(errno));
        return nullptr;
    }

    // init socket
    if ([&interface, &addr, &soc]() -> bool {
            // set reuse
            int opt = 1;
            if (setsockopt(soc, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &opt, sizeof(opt)))
            {
                spdlog::error("[Service::create] Fail to reuse server socket. Error{}: {}",
                              errno, strerror(errno));
                return false;
            }

            // bind
            if (bind(soc, (struct sockaddr *)&addr, sizeof(addr)))
            {
                spdlog::error("[Service::create] bind on interface[{}] fail", interface.c_str());
                return false;
            }

            // listen
            if (listen(soc, SOMAXCONN) == -1)
            {
                spdlog::error("[Service::create] Listen on interface[{}] fail", interface.c_str());
                return false;
            }

            return true;
        }())
    {
        // create service endpoint object
        Service *pService = new Service(soc);
        if (pService)
        {
            pService->mInterface = interface;
            pService->mIp = ip;
            pService->mServicePort = servicePort;
            pService->mTargetHost = targetHost;
            pService->mTargetPort = targetPort;

            return pService;
        }
        else
        {
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

string Service::toStr()
{
    stringstream ss;

    ss << "[Service,"
       << soc << ","
       << mInterface << ","
       << mIp << ","
       << mServicePort << ","
       << mTargetHost << ","
       << mTargetPort << ","
       << tag << "]";

    return ss.str();
}

bool Service::getIfSockAddr(string &interface, string &ip, sockaddr_in &sa)
{
    if (interface.empty() || !strcasecmp(interface.c_str(), "any"))
    {
        spdlog::debug("[Service::getIfSockAddr] set interface to ANY");
        sa.sin_family = AF_INET;
        sa.sin_addr.s_addr = INADDR_ANY;
        sa.sin_port = 0;
        return true;
    }

    struct ifaddrs *ifaddr;

    if (getifaddrs(&ifaddr))
    {
        spdlog::error("[Service::getIfSockAddr] getifaddrs fail: {} - {}", errno, strerror(errno));
        return false;
    }

    bool result = [&]() -> bool {
        for (auto ifa = ifaddr; ifa != nullptr; ifa = ifa->ifa_next)
        {
            if (ifa->ifa_addr == nullptr ||
                strcasecmp(ifa->ifa_name, interface.c_str()) ||
                ifa->ifa_addr->sa_family != AF_INET)
            {
                continue;
            }
            sa = *(sockaddr_in *)ifa->ifa_addr;
            char buffer[INET_ADDRSTRLEN];
            inet_ntop(AF_INET, &sa, buffer, INET_ADDRSTRLEN);
            ip = buffer;

            spdlog::debug("[Service::getIfSockAddr] interface {} --> {}", interface, ip);
            return true;
        }

        spdlog::error("[Service::getIfSockAddr] interface[{}] not found", interface);
        return false;
    }();

    freeifaddrs(ifaddr);
}

} // namespace link
} // namespace mapper
