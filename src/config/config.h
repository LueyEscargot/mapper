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
#ifndef __MAPPER_CONFIG_CONFIG_H__
#define __MAPPER_CONFIG_CONFIG_H__

#include <map>
#include <memory>
#include <regex>
#include <set>
#include <string>
#include <vector>
#include "forward.h"

namespace mapper
{
namespace config
{

class Config
{
protected:
    static const char *DEFAULT_CONFIG_FILE;
    static const char *GLOBAL_SECTION;

    static std::regex REG_FOR_TRIM_LEADING_SPACE;
    static std::regex REG_FOR_TRIM_TAIL_SPACE;
    static std::regex REG_FOR_TRIM_COMMENTS;
    static std::regex REG_SECTION;
    static std::regex REG_CONFIG;
    static std::regex REG_VALID_UNSIGNED_NUMBER;

    static const int DEFAULT_LINK_TUNNELS = 1048576;
    static const int DEFAULT_LINK_NORTHBUF = 1;
    static const int DEFAULT_LINK_SOUTHBUF = 1;

    static const int BUF_SIZE_UNIT = 1 << 20;

    using CONFIG = std::map<std::string, std::map<std::string, std::string>>; // secion - key - value

    struct ClassForwardCompare
    {
        inline bool operator()(const std::shared_ptr<Forward> &l, const std::shared_ptr<Forward> &r) const
        {
            return l->service < r->service;
        }
    };
    using FORWARDS = std::map<std::string, std::set<std::shared_ptr<Forward>, ClassForwardCompare>>; // section - forward settings

public:
    Config();
    Config(int argc, char *argv[]);

    std::string getSyntax();
    void initLog();

    bool parse(int argc, char *argv[]);
    bool parse(const char *file, bool silence = false);
    // void parse(const char *file, std::vector<std::string> &argMapData);
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
    std::vector<std::shared_ptr<Forward>> getForwards(std::string section = "*");
    inline std::vector<std::shared_ptr<Forward>> getMapData(std::string section = "*") { return getForwards(section); }

    // properties of link-tunnels
    inline int getLinkTunnels() { return getAsUint32("tunnels", "link", DEFAULT_LINK_TUNNELS); }
    inline int getLinkNorthBuf() { return BUF_SIZE_UNIT * getAsUint32("northBuf", "link", DEFAULT_LINK_NORTHBUF); }
    inline int getLinkSouthBuf() { return BUF_SIZE_UNIT * getAsUint32("southBuf", "link", DEFAULT_LINK_SOUTHBUF); }

    std::string mConfigFile;

protected:
    inline bool hasArg(char **begin, char **end, const std::string &arg) { return std::find(begin, end, arg) != end; }
    inline const char *getArg(char **begin, char **end, const std::string &arg)
    {
        char **itr = std::find(begin, end, arg);
        return itr != end && ++itr != end ? *itr : "";
    }

    void parseLine(std::string &line);

    std::string mAppName;
    std::string mCurSection;

    CONFIG mConfig;
    FORWARDS mForwards;
};

} // namespace config
} // namespace mapper

#endif // __MAPPER_CONFIG_CONFIG_H__
