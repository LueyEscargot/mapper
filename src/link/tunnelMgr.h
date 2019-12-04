#ifndef __MAPPER_LINK_TUNNELMGR_H__
#define __MAPPER_LINK_TUNNELMGR_H__

#include <list>
#include <string>
#include "type.h"
#include "../config/config.h"

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

    bool init(config::Config *pCfg);
    void close();

    Tunnel_t *allocTunnel(EndpointService_t *pes);
    void freeTunnel(Tunnel_t *pt);

    static bool connect(Tunnel_t *pet);
    static bool onSoc(uint64_t curTime, EndpointRemote_t *per, uint32_t events);
    static bool init(Tunnel_t *pet, int southSoc);

protected:
    Tunnel_t *mpTunnels;
    uint32_t mTunnelCounts;
    std::list<Tunnel_t *> mFreeList;
};

} // namespace link
} // namespace mapper

#endif // __MAPPER_LINK_TUNNELMGR_H__
