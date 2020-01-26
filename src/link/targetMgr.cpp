#include "targetMgr.h"
#include <spdlog/spdlog.h>
#include "utils.h"

using namespace std;

namespace mapper
{
namespace link
{

TargetManager::TargetManager()
{
}

TargetManager::~TargetManager()
{
}

bool TargetManager::addTarget(time_t curTime,
                              int serviceId,
                              const char *host,
                              const char *service,
                              const Protocol_t protocol)
{
    addrinfo *pAddrInfo;
    if (!Utils::getAddrInfo(host,
                            service,
                            protocol,
                            &pAddrInfo))
    {
        spdlog::debug("[TargetManager::addTarget] get addr fail: {}:{}({})",
                      host, service, protocol);
    }

    addrinfo *p = pAddrInfo;
    while (p)
    {
        assert(p->ai_family == AF_INET);
        appendAddrItem(serviceId, (sockaddr_in *)p->ai_addr);

        spdlog::trace("[TargetManager::addTarget] {}:{} -> {}",
                      host, service, Utils::dumpSockAddr(p->ai_addr));

        p = p->ai_next;
    }
    Utils::closeAddrInfo(pAddrInfo);

    return true;
}

const sockaddr_in *TargetManager::getAddr(time_t curTime, int serviceId)
{
    auto it = mId2AddrArray.find(serviceId);
    if (it == mId2AddrArray.end())
    {
        spdlog::error("[TargetManager::getAddr] id[{}] not exist.", serviceId);
        return nullptr;
    }

    ++mId2AddrArrayIndex[serviceId];
    mId2AddrArrayIndex[serviceId] %= mId2AddrArrayLength[serviceId];

    return &it->second[mId2AddrArrayIndex[serviceId]];
}

void TargetManager::failReport(time_t curTime, int serviceId, sockaddr_in *sa)
{
    // do nothing at this version
}

void TargetManager::appendAddrItem(int serviceId, sockaddr_in *addr)
{
    auto it = mId2AddrArray.find(serviceId);
    if (it == mId2AddrArray.end())
    {
        // 新 service id
        mId2AddrArrayIndex[serviceId] = 0;
        mId2AddrArrayLength[serviceId] = 1;
    }
    else
    {
        // 已有 service id 存在
        ++mId2AddrArrayLength[serviceId];
    }

    mId2AddrArray[serviceId].push_back(*addr);
}

} // namespace link
} // namespace mapper
