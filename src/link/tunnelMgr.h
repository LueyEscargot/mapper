#ifndef __MAPPER_LINK_TUNNELMGR_H__
#define __MAPPER_LINK_TUNNELMGR_H__

#include <list>
#include <string>
#include "type.h"

namespace mapper
{
namespace link
{

class TunnelMgr
{
protected:
    TunnelMgr(const TunnelMgr &){};
    TunnelMgr &operator=(const TunnelMgr &) { return *this; }

public:
    TunnelMgr();
    ~TunnelMgr();

    bool init(uint32_t maxTunnels);
    void close();

    Tunnel_t *allocTunnel(Protocol_t protocol,
                          const char *intf,
                          const char *service,
                          const char *targetHost,
                          const char *targetService);
    inline Tunnel_t *allocTunnel(const EndpointService_t *pes, Tunnel_t *pet)
    {
        return TunnelMgr::allocTunnel(pes->protocol,
                                      pes->interface,
                                      pes->service,
                                      pes->targetHost,
                                      pes->targetService);
    }

    static void freeTunnel(Tunnel_t *pTunnel);

    static std::string toStr(const Tunnel_t *pTunnel);

protected:
    static bool getAddrInfo(const EndpointService_t *pes, Tunnel_t *pet);
    static bool connectToTarget(const EndpointService_t *pes, Tunnel_t *pet);

    Tunnel_t *mpTunnels;
    std::list<Tunnel_t *> mFreeList;
};

} // namespace link
} // namespace mapper

#endif // __MAPPER_LINK_TUNNELMGR_H__
