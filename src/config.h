/**
 * @file config.h
 * @author Liu Yu (source@liuyu.com)
 * @brief Class for parse config data
 * @version 1.0
 * @date 2019-10-25
 * 
 * @copyright Copyright (c) 2019
 * 
 */
#ifndef __MAPPER_CONFIG_H__
#define __MAPPER_CONFIG_H__

#include <exception>
#include <string>

namespace mapper
{

class Config
{
public:
    static const char *GLOBAL_SECTION;

    bool parse(const char *cfgUri);
    std::string get(const char *_key, const char *_section = nullptr);
};

} // namespace mapper

#endif // __MAPPER_CONFIG_H__
