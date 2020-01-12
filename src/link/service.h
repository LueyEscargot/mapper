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
    static const uint32_t SEETING_TIMEOUT_CONNECT = 15;
    static const uint32_t SEETING_TIMEOUT_SESSION = 180;
    static const uint32_t SEETING_TIMEOUT_RELEASE = 15;
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
        // buffer
        uint32_t bufferSize;
        uint32_t bufferPerSessionLimit;
    };

public:
    Service(std::string name);
    virtual ~Service() {}

    static bool create(int epollfd,
                       buffer::DynamicBuffer *pBuffer,
                       rapidjson::Document &cfg,
                       std::list<Service *> &serviceList);
    static void release(std::list<Service *> &serviceList);
    static void loadSetting(rapidjson::Document &cfg, Setting_t &setting);
    static std::string dumpSetting(Setting_t &setting);

    bool init(int epollfd, buffer::DynamicBuffer *pBuffer);
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
    buffer::DynamicBuffer *mpBuffer;
};

} // namespace link
} // namespace mapper

#endif // __MAPPER_LINK_SERVER_H__
