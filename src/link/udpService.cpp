#include "udpService.h"
#include <assert.h>
#include <fcntl.h>
#include <ifaddrs.h>
#include <netdb.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <sstream>
#include <spdlog/spdlog.h>
#include "endpoint.h"
#include "udpTunnel.h"
#include "../buffer/buffer.h"

using namespace std;

namespace mapper
{
namespace link
{

UdpService::UdpService(int epollfd)
    : mEpollfd(epollfd),
      mpDynamicBuffer(nullptr),
      mToNorthStop(false),
      mToSouthStop(false),
      mpTunnels(nullptr),
      mTunnelCounts(0)
{
}

UdpService::~UdpService()
{
}

bool UdpService::init(uint32_t tunnelCount, uint32_t bufferCapacity)
{
    if (mpTunnels)
    {
        spdlog::error("[UdpTunnelMgr::init] instance initialized already.");
        return false;
    }

    mTunnelCounts = tunnelCount;
    spdlog::debug("[UdpTunnelMgr::init] init for {} tunnels.", mTunnelCounts);

    mpTunnels = static_cast<UdpTunnel_t *>(malloc(sizeof(UdpTunnel_t) * mTunnelCounts));
    if (!mpTunnels)
    {
        spdlog::error("[UdpTunnelMgr::init] create tunnel array fail.");
        return false;
    }
    else
    {
        for (int i = 0; i < mTunnelCounts; ++i)
        {
            mpTunnels[i].create();
        }
    }

    // alloc dynamic buffer
    mpDynamicBuffer = buffer::DynamicBuffer::alloc(bufferCapacity);
    if (mpDynamicBuffer == nullptr)
    {
        spdlog::error("[UdpTunnelMgr::init] alloc dynamic buffer fail.");
        close();
        return false;
    }

    return true;
}

void UdpService::close()
{
    // release tunnels
    if (mpTunnels)
    {
        spdlog::debug("[UdpTunnelMgr::close] release tunnels.");
        for (int i = 0; i < mTunnelCounts; ++i)
        {
            mpTunnels[i].close();
        }

        // release tunnel array
        free(mpTunnels);
        mpTunnels = nullptr;
        mTunnelCounts = 0;
    }

    // release dynamic buffer
    if (mpDynamicBuffer)
    {
        spdlog::debug("[UdpTunnelMgr::close] release dynamic buffer.");
        buffer::DynamicBuffer::release(mpDynamicBuffer);
        mpDynamicBuffer = nullptr;
    }

    // TODO: release allocated dynamic buffer object(stored in udp tunnel object)
}

UdpTunnel_t *UdpService::allocTunnel()
{
    if (mFreeList.empty())
    {
        return nullptr;
    }

    UdpTunnel_t *pt = mFreeList.front();
    mFreeList.pop_front();

    UdpTunnel::setStatus(pt, TunnelState_t::ALLOCATED);

    return pt;
}

void UdpService::freeTunnel(UdpTunnel_t *pt)
{
    if (pt->status != TunnelState_t::BROKEN)
    {
        spdlog::critical("[UdpTunnelMgr::freeTunnel] invalid status: {}", pt->status);
        assert(false);
    }

    // close tunnel
    pt->close();
    UdpTunnel::setStatus(pt, TunnelState_t::CLOSED);

    mFreeList.push_front(pt);
}

void UdpService::onSouthSoc(time_t curTime, uint32_t events, link::EndpointService_t *pes)
{

    sockaddr_in sai;
    socklen_t saiLen = sizeof(sockaddr_in);

    void *pBuf = mpDynamicBuffer->reserve(MAX_UDP_BUFFER);
    if (pBuf == nullptr)
    {
        // out of memory
        spdlog::trace("[UdpService::onSouthSoc] out of memory");
        mToNorthStop = true;
        return;
    }

    while (true)
    {
        sockaddr_in addr;
        socklen_t addrLen = sizeof(sockaddr_in);
        int nRet = recvfrom(pes->soc, pBuf, MAX_UDP_BUFFER, 0, (sockaddr *)&addr, &addrLen);
        if (nRet > 0)
        {
            buffer::DynamicBuffer::BufBlk_t *pBufBlk = mpDynamicBuffer->cut(nRet);

            // 查找/分配对应 UDP tunnel

            // 处理数据包
        }
        else if (nRet < 0)
        {
            if (errno == EAGAIN)
            {
                // 此次数据接收已完毕
                break;
            }
            else
            {
                spdlog::error("[UdpService::onSouthSoc] soc[{}] recv fail: {}:[]",
                              pes->soc, errno, strerror(errno));
                pes->valid = false;
                mToNorthStop = true;
                return;
            }
        }
        else
        {
            spdlog::trace("[UdpService::onSouthSoc] skip empty udp packet.");
        }
    }
}

void UdpService::onNorthSoc(time_t curTime, uint32_t events, link::EndpointRemote_t *per)
{
    spdlog::critical("[UdpTunnelMgr::onNorthSoc] NOT IMPL YET ！");
}

} // namespace link
} // namespace mapper
