#include "dnsReqMgr.h"
#include <spdlog/spdlog.h>

using namespace std;

namespace mapper
{
namespace link
{

DnsReqMgr::DnsReqMgr()
    : mpNameResolveBlks(nullptr)
{
}

DnsReqMgr::~DnsReqMgr()
{
    close();
}

bool DnsReqMgr::init(const uint32_t maxDnsReqs)
{
    spdlog::debug("[DnsReqMgr::init] init for {} blocks.");

    if (mpNameResolveBlks)
    {
        spdlog::error("[DnsReqMgr::init] instance initialized already.");
        return false;
    }

    mpNameResolveBlks = static_cast<NameResolveBlk_t *>(malloc(sizeof(NameResolveBlk_t) * maxDnsReqs));
    if (!mpNameResolveBlks)
    {
        spdlog::error("[DnsReqMgr::init] create block array fail.");
        return false;
    }

    for (int i = 0; i < maxDnsReqs; ++i)
    {
        mpNameResolveBlks[i].init();
        mFreeList.push_back(mpNameResolveBlks + i);
    }
}

void DnsReqMgr::close()
{
    if (mpNameResolveBlks)
    {
        mFreeList.clear();
        free(mpNameResolveBlks);
        mpNameResolveBlks = nullptr;
    }
}

NameResolveBlk_t *DnsReqMgr::allocBlk(const char *host, const int port, int socktype, int protocol, int flags)
{
    if (!mFreeList.empty())
    {
        NameResolveBlk_t *pBlk = mFreeList.front();
        pBlk->init(host, port, socktype, protocol, flags);
        mFreeList.pop_front();
        return pBlk;
    }
    else
    {
        return nullptr;
    }
}

void DnsReqMgr::releaseBlk(void *pNameResBlk)
{
    if (pNameResBlk)
    {
        mFreeList.push_front(static_cast<NameResolveBlk_t *>(pNameResBlk));
    }
}

} // namespace link
} // namespace mapper
