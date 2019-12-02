/**
 * @file dnsReqMgr.h
 * @author Liu Yu (source@liuyu.com)
 * @brief Class for manage DNS request blocks.
 * @version 1.0
 * @date 2019-12-02
 * 
 * @copyright Copyright (c) 2019
 * 
 */

#ifndef __MAPPER_LINK_DNSREQMGR_H__
#define __MAPPER_LINK_DNSREQMGR_H__

#include <stdio.h>
#include <list>
#include "type.h"

namespace mapper
{
namespace link
{

class DnsReqMgr
{
public:
    DnsReqMgr();
    ~DnsReqMgr();

    bool init(const uint32_t maxDnsReqs);
    void close();

    NameResBlk_t *allocBlk(const char *host, const int port, int socktype, int protocol, int flags);
    void releaseBlk(void *pNameResBlk);

protected:
    NameResBlk_t *mNameResBlkArray;
    std::list<NameResBlk_t *> mFreeList;
};

} // namespace link
} // namespace mapper

#endif // __MAPPER_LINK_DNSREQMGR_H__
