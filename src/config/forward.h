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

#include <string>

namespace mapper
{
namespace config
{

class Forward
{
public:
    inline Forward(const std::string &protocol,
            const std::string &interface,
            const uint32_t servicePort,
            const std::string &targetHost,
            const uint32_t targetPort)
    {
        init(protocol,
             interface,
             servicePort,
             targetHost,
             targetPort);
    }
    inline Forward(const Forward &src) : Forward(src.protocol,
                                          src.interface,
                                          src.servicePort,
                                          src.targetHost,
                                          src.targetPort) {}
    inline Forward &operator=(const Forward &src)
    {
        init(src.protocol,
             src.interface,
             src.servicePort,
             src.targetHost,
             src.targetPort);
    }
    inline void init(const std::string &protocol,
                     const std::string &interface,
                     const uint32_t servicePort,
                     const std::string &targetHost,
                     const uint32_t targetPort)
    {
        this->protocol = protocol;
        this->interface = interface;
        this->servicePort = servicePort;
        this->targetHost = targetHost;
        this->targetPort = targetPort;
    }

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
