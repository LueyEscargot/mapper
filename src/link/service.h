/**
 * @file service.h
 * @author Liu Yu (source@liuyu.com)
 * @brief Base Class of Network Service.
 * @version 2
 * @date 2019-12-05
 * 
 * @copyright Copyright (c) 2019
 * 
 */
#ifndef __MAPPER_LINK_SERVER_H__
#define __MAPPER_LINK_SERVER_H__

#include <time.h>
#include <list>
#include <memory>
#include <string>
#include <rapidjson/document.h>
#include "type.h"
#include "../buffer/dynamicBuffer.h"

namespace mapper
{
namespace link
{

class Service
{
protected:
    static const uint32_t SEETING_TIMEOUT_CONNECT = 3;
    static const uint32_t SEETING_TIMEOUT_SESSION = 180;
    static const uint32_t SEETING_TIMEOUT_RELEASE = 3;
    static const uint32_t SEETING_TIMEOUT_UDP = 5;
    static const uint32_t SEETING_BUFFER_SIZE = 128;
    static const uint32_t SEETING_BUFFER_PERSESSIONLIMIT = 1;
    static const uint32_t SEETING_BUFFER_SIZE_UNIT = 1048576; // 1MB

    static const std::string CONFIG_BASE_PATH;

    struct Setting_t
    {
        // timeout
        uint32_t connectTimeout;
        uint32_t sessionTimeout;
        uint32_t releaseTimeout;
        uint32_t udpTimeout;
        // buffer
        uint64_t bufferSize;
        uint64_t bufferPerSessionLimit;
    };

    Service(const char *name) { mName = name; };
    virtual ~Service() {}

public:
    static bool create(rapidjson::Document &cfg,
                       std::list<Service *> &serviceList);
    static void release(std::list<Service *> &serviceList);

    virtual void join() = 0;
    virtual void stop() = 0;
    virtual void close() = 0;

    inline std::string name() { return mName; }

protected:
    static void loadSetting(rapidjson::Document &cfg, Setting_t &setting);

    static bool epollAddEndpoint(int epollfd, Endpoint_t *pe, bool read, bool write, bool edgeTriger);
    static bool epollResetEndpointMode(int epollfd, Endpoint_t *pe, bool read, bool write, bool edgeTriger);
    static bool epollResetEndpointMode(int epollfd, Tunnel_t *pt, bool read, bool write, bool edgeTriger);
    static void epollRemoveEndpoint(int epollfd, Endpoint_t *pe);
    static void epollRemoveTunnel(int epollfd, Tunnel_t *pt);

    std::string mName;
};

} // namespace link
} // namespace mapper

#endif // __MAPPER_LINK_SERVER_H__
