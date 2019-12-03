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

    Tunnel_t *allocTunnel(EndpointService_t *pes);
    void freeTunnel(Tunnel_t *pt);

protected:
    static bool init(const EndpointService_t *pes, int clientSoc, Tunnel_t *pet);
    static bool connectToTarget(Tunnel_t *pet);

    Tunnel_t *mpTunnels;
    std::list<Tunnel_t *> mFreeList;
};

} // namespace link
} // namespace mapper

#endif // __MAPPER_LINK_TUNNELMGR_H__
