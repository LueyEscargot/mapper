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
#include <memory>
#include <string>
#include "type.h"

namespace mapper
{
namespace link
{

class Service
{
public:
    Service(std::string name);
    virtual ~Service() {}

    bool init(int epollfd);
    inline std::string &getName() { return mName; }
    inline Endpoint_t &getServiceEndpoint() { return mServiceEndpoint; }

    virtual void close() = 0;
    virtual void onSoc(time_t curTime, uint32_t events, Endpoint_t *pe) = 0;
    virtual void postProcess(time_t curTime) = 0;
    virtual void scanTimeout(time_t curTime) = 0;

protected:
    int mEpollfd;
    std::string mName;
    Endpoint_t mServiceEndpoint;
};

} // namespace link
} // namespace mapper

#endif // __MAPPER_LINK_SERVER_H__
