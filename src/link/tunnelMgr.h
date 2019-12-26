/**
 * @file tunnelMgr.h
 * @author Liu Yu (source@liuyu.com)
 * @brief tunnel manager.
 * @version 1.0
 * @date 2019-12-02
 * 
 * @copyright Copyright (c) 2019
 * 
 */
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

    Tunnel_t *allocTunnel();
    void freeTunnel(Tunnel_t *pt);

protected:
    Tunnel_t *mpTunnels;
    uint32_t mTunnelCounts;
    std::list<Tunnel_t *> mFreeList;
};

} // namespace link
} // namespace mapper

#endif // __MAPPER_LINK_TUNNELMGR_H__
