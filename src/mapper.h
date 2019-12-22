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
#include "netMgr.h"
#include "config/config.h"

namespace mapper
{

class Mapper
{
public:
    Mapper();
    ~Mapper();

    bool run(config::Config &cfg);

protected:
    void release();

    NetMgr mNetMgr;
};

} // namespace mapper

#endif // __MAPPER_MAPPER_H__
