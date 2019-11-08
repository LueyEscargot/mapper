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
#include <map>
#include <regex>
#include <string>
#include <vector>
#include "define.h"

namespace mapper
{

class Config
{
protected:
    static const char *GLOBAL_SECTION;

    static std::regex REG_FOR_TRIM_LEADING_SPACE;
    static std::regex REG_FOR_TRIM_TAIL_SPACE;
    static std::regex REG_FOR_TRIM_COMMENTS;
    static std::regex REG_SECTION;
    static std::regex REG_CONFIG;
    static std::regex REG_MAPPING;
    static std::regex REG_VALID_IPV4;

    using SECTION = std::map<std::string, std::string>;
    using CONFIG = std::map<std::string, SECTION>;
    using TARGET = std::pair<std::string, int>;

public:
    Config();

    bool parse(const char *file, bool silence = false);
    void parse(const char *file, std::vector<std::string> &argMapData);
    std::string get(std::string key, std::string section = "", std::string defaultValue = "");
    std::vector<MapData_t> &getMapData();

protected:
    void parseLine(std::string &line);

    std::string mCurSection;
    CONFIG mConfig;
    std::map<int, TARGET> mRawMapData;
    std::vector<MapData_t> mMapDatas;
};

} // namespace mapper

#endif // __MAPPER_CONFIG_H__
