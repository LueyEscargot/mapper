/**
 * @file forward.h
 * @author Liu Yu (source@liuyu.com)
 * @brief Class for forward parse & store settings. Move from mapper::config
 * @version 1.0
 * @date 2020-01-10
 * 
 * @copyright Copyright (c) 2020
 * 
 */

#ifndef __MAPPER_LINK_FORWARD_H__
#define __MAPPER_LINK_FORWARD_H__

#include <memory>
#include <regex>
#include <string>

namespace mapper
{
namespace link
{

class Forward
{
protected:
    static const std::regex REG_FORWARD_SETTING;

public:
    Forward() {}
    Forward(const std::string &protocol,
            const std::string &interface,
            const std::string service,
            const std::string &targetHost,
            const std::string targetService);
    Forward(const Forward &src);
    Forward(const Forward *src);
    Forward &operator=(const Forward &src);

    void init(const std::string &protocol,
              const std::string &interface,
              const std::string service,
              const std::string &targetHost,
              const std::string targetService);

    static std::shared_ptr<Forward> create(std::string setting);
    static void release(std::shared_ptr<Forward> pForward);

    bool parse(std::string &setting);
    std::string toStr();

    std::string protocol;
    std::string interface;
    std::string service;
    std::string targetHost;
    std::string targetService;
};

} // namespace link
} // namespace mapper

#endif // __MAPPER_LINK_FORWARD_H__
