/**
 * @file targetMgr.h
 * @author Liu Yu (source@liuyu.com)
 * @brief Target Manager.
 * @version 1.0
 * @date 2020-01-03
 * 
 * @copyright Copyright (c) 2019
 * 
 */
#ifndef __MAPPER_LINK_TARGETMGR_H__
#define __MAPPER_LINK_TARGETMGR_H__

#include <netdb.h>
#include <time.h>
#include <map>
#include "utils.h"

namespace mapper
{
namespace link
{

class TargetManager
{
protected:
    struct AddrItem_t
    {
        AddrItem_t *prev;
        AddrItem_t *next;
        sockaddr addr;
        socklen_t addrLen;

        bool valid;
        time_t updateTime;
        time_t lastErrTime;

        void init(time_t curTime, addrinfo *p)
        {
            prev = nullptr;
            next = nullptr;
            addr = *p->ai_addr;
            addrLen = p->ai_addrlen;
            valid = true;
            updateTime = curTime;
            lastErrTime = 0;
        }
    };

    TargetManager(const TargetManager &) = default;
    TargetManager &operator=(const TargetManager &) { return *this; }

public:
    TargetManager();
    virtual ~TargetManager();

    bool addTarget(time_t curTime,
                   const char *host,
                   const char *service,
                   const Protocol_t protocol);
    AddrItem_t *getAddr(time_t curTime);
    void failReport(time_t curTime, sockaddr *sa);
    void clear();

protected:
    void appendAddrItem(TargetManager::AddrItem_t *ai);

    AddrItem_t *mpAddrsHead;
    AddrItem_t *mpCurAddr;
    std::map<sockaddr, AddrItem_t *, Utils::AddrCmp_t> mAddr2Item;
};

} // namespace link
} // namespace mapper

#endif // __MAPPER_LINK_TARGETMGR_H__
