/**
 * @file mapper.h
 * @author Liu Yu (source@liuyu.com)
 * @brief Project main service routing.
 * @version 2.0
 * @date 2019-10-07
 * 
 * @copyright Copyright (c) 2019-2020
 * 
 */

#ifndef __MAPPER_MAPPER_H__
#define __MAPPER_MAPPER_H__

#include <list>
#include <vector>
#include <rapidjson/document.h>
#include "link/service.h"

namespace mapper
{

class Mapper
{
    static const uint32_t STATISTIC_INTERVAL;

public:
    Mapper();
    ~Mapper(){};

    bool run(rapidjson::Document &cfg);
    void stop();

protected:
    void join();

    std::list<link::Service *> mServiceList;
    volatile bool mStop;
};

} // namespace mapper

#endif // __MAPPER_MAPPER_H__
