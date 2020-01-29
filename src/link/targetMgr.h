/**
 * @file targetMgr.h
 * @author Liu Yu (source@liuyu.com)
 * @brief Target Manager.
 * @version 1.0
 * @date 2020-01-03
 * 
 * @copyright Copyright (c) 2019-2020
 * 
 */
#ifndef __MAPPER_LINK_TARGETMGR_H__
#define __MAPPER_LINK_TARGETMGR_H__

#include <map>
#include <vector>
#include "type.h"

namespace mapper
{
namespace link
{

class TargetManager
{
protected:
    TargetManager(const TargetManager &) = default;
    TargetManager &operator=(const TargetManager &) { return *this; }

public:
    TargetManager();
    virtual ~TargetManager();

    bool addTarget(int id,
                   const char *host,
                   const char *service,
                   const Protocol_t protocol);
    const sockaddr_in *getAddr(int id);
    void failReport(int id, const sockaddr_in *sa);
    void clear();

protected:
    void appendAddrItem(int id, sockaddr_in *addr);

    std::map<int, std::vector<sockaddr_in>> mId2AddrArray;
    std::map<int, uint32_t> mId2AddrArrayIndex;
    std::map<int, uint32_t> mId2AddrArrayLength;
};

} // namespace link
} // namespace mapper

#endif // __MAPPER_LINK_TARGETMGR_H__
