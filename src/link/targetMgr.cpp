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

bool TargetManager::addTarget(int id,
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
        spdlog::error("[TargetManager::addTarget] get addr fail: {}:{}({})",
                      host, service, protocol);
        return false;
    }

    addrinfo *p = pAddrInfo;
    while (p)
    {
        assert(p->ai_family == AF_INET);
        appendAddrItem(id, (sockaddr_in *)p->ai_addr);

        spdlog::trace("[TargetManager::addTarget] {} -> {}",
                      id, Utils::dumpSockAddr(p->ai_addr));

        p = p->ai_next;
    }
    Utils::closeAddrInfo(pAddrInfo);

    return true;
}

const sockaddr_in *TargetManager::getAddr(int id)
{
    auto it = mId2AddrArray.find(id);
    if (it == mId2AddrArray.end())
    {
        spdlog::error("[TargetManager::getAddr] id[{}] not exist.", id);
        return nullptr;
    }

    ++mId2AddrArrayIndex[id];
    mId2AddrArrayIndex[id] %= mId2AddrArrayLength[id];

    return &it->second[mId2AddrArrayIndex[id]];
}

void TargetManager::failReport(int id, const sockaddr_in *sa)
{
    // do nothing at this version
}

void TargetManager::clear()
{
    mId2AddrArray.clear();
    mId2AddrArrayIndex.clear();
    mId2AddrArrayLength.clear();
}

void TargetManager::appendAddrItem(int id, sockaddr_in *addr)
{
    auto it = mId2AddrArray.find(id);
    if (it == mId2AddrArray.end())
    {
        // 新 service id
        mId2AddrArrayIndex[id] = 0;
        mId2AddrArrayLength[id] = 1;
    }
    else
    {
        // 已有 service id 存在
        ++mId2AddrArrayLength[id];
    }

    mId2AddrArray[id].push_back(*addr);
}

} // namespace link
} // namespace mapper
