#include "targetMgr.h"
#include <spdlog/spdlog.h>
#include "utils.h"

using namespace std;

namespace mapper
{
namespace link
{

TargetManager::TargetManager()
    : mpAddrsHead(nullptr), mpCurAddr(nullptr)
{
}

TargetManager::~TargetManager()
{
    clear();
}

bool TargetManager::addTarget(time_t curTime,
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
        AddrItem_t *ai = new AddrItem_t;
        if (!ai)
        {
            spdlog::debug("[TargetManager::addTarget] create address item fail");
            Utils::closeAddrInfo(pAddrInfo);
            return false;
        }
        ai->init(curTime, p);
        appendAddrItem(ai);

        spdlog::trace("[TargetManager::addTarget] {}:{} -> {}",
                      host, service, Utils::dumpSockAddr(&ai->addr));

        p = p->ai_next;
    }
    Utils::closeAddrInfo(pAddrInfo);

    return true;
}

TargetManager::AddrItem_t *TargetManager::getAddr(time_t curTime)
{
    if (!mpCurAddr)
    {
        return nullptr;
    }

    auto p = mpCurAddr;
    auto startPos = p;
    mpCurAddr = mpCurAddr->next;

    bool success = false;
    while (true)
    {
        if (p->valid)
        {
            success = true;
            break;
        }
        else
        {
            if (p->lastErrTime < curTime)
            {
                p->valid = true;
                success = true;
                break;
            }

            p = p->next;
            if (p = startPos) {
                break;
            }
        }
    }

    return success ? p : nullptr;
}

void TargetManager::failReport(time_t curTime, sockaddr *sa)
{
    auto it = mAddr2Item.find(*sa);
    if (it == mAddr2Item.end())
    {
        return;
    }

    auto p = it->second;
    p->valid = false;
    p->lastErrTime = curTime;
}

void TargetManager::clear()
{
    if (!mAddr2Item.empty())
    {
        spdlog::debug("[TargetManager::clear] clean mAddr2Item");
        for (auto it : mAddr2Item)
        {
            delete (it.second);
        }
        mAddr2Item.clear();
    }

    mpAddrsHead = nullptr;
    mpCurAddr = nullptr;
}

void TargetManager::appendAddrItem(TargetManager::AddrItem_t *ai)
{
    if (!mpAddrsHead)
    {
        mpAddrsHead =
            mpCurAddr =
                ai->prev =
                    ai->next =
                        ai;
        return;
    }

    ai->next = mpAddrsHead;
    ai->prev = mpAddrsHead->prev;

    mpAddrsHead->prev->next = ai;
    mpAddrsHead->prev = ai;

    mAddr2Item[ai->addr] = ai;
}

} // namespace link
} // namespace mapper
