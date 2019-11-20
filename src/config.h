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
    static std::regex REG_VALID_UNSIGNED_NUMBER;

    using SECTION = std::map<std::string, std::string>;
    using CONFIG = std::map<std::string, SECTION>;
    using TARGET = std::pair<std::string, int>;

public:
    Config();

    bool parse(const char *file, bool silence = false);
    void parse(const char *file, std::vector<std::string> &argMapData);
    std::string get(std::string key, std::string section = "", std::string defaultValue = "");
    uint32_t getAsUint32(std::string key, std::string section = "", uint32_t defaultValue = 0);

    inline uint32_t getSessions(uint32_t defaultValue = 0)
    {
        return getAsUint32("sessions", "global", defaultValue);
    }
    template <class T>
    inline void setSessions(T sessions) { mConfig["sessions"]["global"] = sessions; }
    inline void setSessions(uint32_t sessions) { setSessions(std::to_string(sessions)); }
    inline uint32_t getBufferSize(uint32_t defaultValue = 0)
    {
        return getAsUint32("bufferSize", "global", defaultValue);
    }
    template <class T>
    inline void setBufferSize(T bufferSize) { mConfig["bufferSize"]["global"] = bufferSize; }
    inline void setBufferSize(uint32_t bufferSize) { setBufferSize(std::to_string(bufferSize)); }
    inline uint32_t getConnectTimeoutInterval(uint32_t defaultValue = 0)
    {
        return getAsUint32("connectTimeout", "global", defaultValue);
    }
    template <class T>
    inline void setConnectTimeoutInterval(T interval) { mConfig["connectTimeout"]["global"] = interval; }
    inline void setConnectTimeoutInterval(uint32_t interval) { setConnectTimeoutInterval(std::to_string(interval)); }
    inline uint32_t getSessionTimeoutInterval(uint32_t defaultValue = 0)
    {
        return getAsUint32("sessionTimeout", "global", defaultValue);
    }
    template <class T>
    inline void setSessionTimeoutInterval(T interval) { mConfig["sessionTimeout"]["global"] = interval; }
    inline void setSessionTimeoutInterval(uint32_t interval) { setSessionTimeoutInterval(std::to_string(interval)); }
    template <class T>
    inline std::string getLogLevel(T &defaultValue = "info")
    {
        return get("level", "log", defaultValue);
    }
    std::vector<MapData_t> getMapData();

protected:
    void parseLine(std::string &line);

    std::string mCurSection;
    CONFIG mConfig;
    std::map<int, TARGET> mRawMapData;
};

} // namespace mapper

#endif // __MAPPER_CONFIG_H__
