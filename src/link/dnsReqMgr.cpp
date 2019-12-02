#include "dnsReqMgr.h"
#include <spdlog/spdlog.h>

using namespace std;

namespace mapper
{
namespace link
{

DnsReqMgr::DnsReqMgr()
    : mNameResBlkArray(nullptr)
{
}

DnsReqMgr::~DnsReqMgr()
{
    close();
}

bool DnsReqMgr::init(const uint32_t maxDnsReqs)
{
    spdlog::debug("[DnsReqMgr::init] init for {} blocks.");

    if (mNameResBlkArray)
    {
        spdlog::error("[DnsReqMgr::init] instance initialized already.");
        return false;
    }

    mNameResBlkArray = static_cast<NameResBlk_t *>(malloc(sizeof(NameResBlk_t) * maxDnsReqs));
    if (!mNameResBlkArray)
    {
        spdlog::error("[DnsReqMgr::init] create block array fail.");
        return false;
    }

    for (int i = 0; i < maxDnsReqs; ++i)
    {
        mNameResBlkArray[i].init();
        mFreeList.push_back(mNameResBlkArray + i);
    }
}

void DnsReqMgr::close()
{
    if (mNameResBlkArray)
    {
        mFreeList.clear();
        free(mNameResBlkArray);
        mNameResBlkArray = nullptr;
    }
}

NameResBlk_t *DnsReqMgr::allocBlk(const char *host, const int port, int socktype, int protocol, int flags)
{
    if (!mFreeList.empty())
    {
        NameResBlk_t *pBlk = mFreeList.front();
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
        mFreeList.push_front(static_cast<NameResBlk_t *>(pNameResBlk));
    }
}

} // namespace link
} // namespace mapper
