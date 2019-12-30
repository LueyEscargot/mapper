/**
 * @file service.h
 * @author Liu Yu (source@liuyu.com)
 * @brief Base Class of Network Services.
 * @version 2
 * @date 2019-12-05
 * 
 * @copyright Copyright (c) 2019
 * 
 */
#ifndef __MAPPER_LINK_SERVER_H__
#define __MAPPER_LINK_SERVER_H__

#include <time.h>
#include <string>
#include "type.h"
#include "../config/config.h"

namespace mapper
{
namespace link
{

class Service
{
public:
    Service(std::string name);
    virtual ~Service();

    virtual std::string toStr();

    virtual bool init(config::Config *pConf, int epollfd) = 0;
    virtual void close() = 0;
    virtual void onSoc(time_t curTime, uint32_t events, Endpoint_t *pe) = 0;

protected:
    std::string mName;
    config::Config *mpConf;
    int mEpollfd;
};

} // namespace link
} // namespace mapper

#endif // __MAPPER_LINK_SERVER_H__
