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
    static std::regex REG_VALID_NUMBER;
    static std::regex REG_VALID_UNSIGNED_NUMBER;

    // default value of global properties
    static const int DEFAULT_GLOBAL_SESSIONS = 256;
    static const int DEFAULT_GLOBAL_CONNECT_TIMEOUT = 15;
    static const int DEFAULT_GLOBAL_SESSION_TIMEOUT = 180;
    static const int DEFAULT_GLOBAL_RELEASE_TIMEOUT = 15;
    // default value of link properties
    static const int DEFAULT_LINK_TUNNELS = 1 << 10;
    static const int DEFAULT_LINK_NORTHBUF = 1;
    static const int DEFAULT_LINK_SOUTHBUF = 1;
    static const int DEFAULT_LINK_UDP_TUNNELS = 1 << 10;
    static const int DEFAULT_LINK_UDP_BUFFER = 1 << 17;

    static const int BUF_SIZE_UNIT = 1 << 10;

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

    bool parse(int argc, char *argv[]);
    bool parse(const char *file, bool silence = false);
    std::string get(std::string key, std::string section = "", std::string defaultValue = "");
    int32_t getAsInt32(std::string key, std::string section = "", int32_t defaultValue = 0);
    uint32_t getAsUint32(std::string key, std::string section = "", uint32_t defaultValue = 0);

    std::vector<std::shared_ptr<Forward>> getForwards(std::string section = "*");

    // properties of global
    inline int getGlobalSessions() { return getAsUint32("sessions", "global", DEFAULT_GLOBAL_SESSIONS); }
    inline int getGlobalConnectTimeout() { return getAsInt32("connectTimeout", "global", DEFAULT_GLOBAL_CONNECT_TIMEOUT); }
    inline int getGlobalSessionTimeout() { return getAsInt32("sessionTimeout", "global", DEFAULT_GLOBAL_SESSION_TIMEOUT); }
    inline int getGlobalReleaseTimeout() { return getAsInt32("releaseTimeout", "global", DEFAULT_GLOBAL_RELEASE_TIMEOUT); }

    // properties of link-tunnels
    inline int getLinkTunnels() { return getAsUint32("tunnels", "link", DEFAULT_LINK_TUNNELS); }
    inline int getLinkNorthBuf() { return BUF_SIZE_UNIT * getAsUint32("northBuf", "link", DEFAULT_LINK_NORTHBUF); }
    inline int getLinkSouthBuf() { return BUF_SIZE_UNIT * getAsUint32("southBuf", "link", DEFAULT_LINK_SOUTHBUF); }
    inline int getLinkUdpTunnels() { return BUF_SIZE_UNIT * getAsUint32("udpTunnels", "link", DEFAULT_LINK_UDP_TUNNELS); }
    inline int getLinkUdpBuffer() { return BUF_SIZE_UNIT * getAsUint32("udpBuffer", "link", DEFAULT_LINK_UDP_BUFFER); }

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
