/**
 * @file mapper.h
 * @author Liu Yu (source@liuyu.com)
 * @brief Project main service routing.
 * @version 1.0
 * @date 2019-10-07
 * 
 * @copyright Copyright (c) 2019
 * 
 */

#ifndef __MAPPER_MAPPER_H__
#define __MAPPER_MAPPER_H__

#include <vector>
#include "define.h"
#include "netMgr.h"

namespace mapper
{

class Mapper
{
public:
    Mapper(uint32_t bufSize);
    ~Mapper();

    bool run(const int maxSessions, std::vector<mapper::MapData_t> &mapDatas);

protected:
    void release();

    NetMgr mNetMgr;
};

} // namespace mapper

#endif // __MAPPER_MAPPER_H__
