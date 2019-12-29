/**
 * @file udpService.h
 * @author Liu Yu (source@liuyu.com)
 * @brief UDP Service Class.
 * @version 1.0
 * @date 2019-12-26
 * 
 * @copyright Copyright (c) 2019
 * 
 */
#ifndef __MAPPER_LINK_UDPSERVICE_H__
#define __MAPPER_LINK_UDPSERVICE_H__

#include <time.h>
#include <sys/socket.h>
#include <list>
#include <map>
#include <string>
#include "type.h"
#include "udpTunnel.h"
#include "../buffer/dynamicBuffer.h"

namespace mapper
{
namespace link
{

class UdpService
{
protected:
    static const uint32_t MAX_UDP_BUFFER = 1 << 16;

    UdpService(const UdpService &){};
    UdpService &operator=(const UdpService &) { return *this; }

public:
    UdpService(int epollfd);
    ~UdpService();

    bool init(uint32_t tunnelCount, uint32_t bufferCapacity);
    void close();

    UdpTunnel_t *allocTunnel();
    void freeTunnel(UdpTunnel_t *pt);

    void onSouthSoc(time_t curTime, uint32_t events, link::EndpointService_t *pes);
    void onNorthSoc(time_t curTime, uint32_t events, link::EndpointRemote_t *per);

protected:
    int mEpollfd;
    buffer::DynamicBuffer *mpDynamicBuffer;
    buffer::DynamicBuffer::BufBlk_t toSouthBufList; // 其中 next 指向链表中第一个元素；prev 指向最后一个。

    bool mToNorthStop;
    bool mToSouthStop;

    UdpTunnel_t *mpTunnels;
    uint32_t mTunnelCounts;
    std::list<UdpTunnel_t *> mFreeList;

    std::map<sockaddr_in, UdpTunnel *> mAddr2Tunnel;
    std::map<int, UdpTunnel *> mSoc2Tunnel;
};

} // namespace link
} // namespace mapper

#endif // __MAPPER_LINK_UDPSERVICE_H__
