/**
 * @file forward.h
 * @author Liu Yu (source@liuyu.com)
 * @brief Class for forward parse & store settings.
 * @version 1.0
 * @date 2019-11-22
 * 
 * @copyright Copyright (c) 2019
 * 
 */

#ifndef __MAPPER_CONFIG_FORWARD_H__
#define __MAPPER_CONFIG_FORWARD_H__

#include <memory>
#include <string>

namespace mapper
{
namespace config
{

class Forward
{
public:
    Forward() {}
    Forward(const std::string &protocol,
            const std::string &interface,
            const uint32_t servicePort,
            const std::string &targetHost,
            const uint32_t targetPort);
    Forward(const Forward &src);
    Forward(const Forward *src);
    Forward &operator=(const Forward &src);

    void init(const std::string &protocol,
              const std::string &interface,
              const uint32_t servicePort,
              const std::string &targetHost,
              const uint32_t targetPort);

    static std::shared_ptr<Forward> create(std::string setting);
    static void release(std::shared_ptr<Forward> pForward);

    bool parse(std::string &setting);
    std::string toStr();

    std::string protocol;
    std::string interface;
    uint32_t servicePort;
    std::string targetHost;
    uint32_t targetPort;
};

} // namespace config
} // namespace mapper

#endif // __MAPPER_CONFIG_FORWARD_H__
