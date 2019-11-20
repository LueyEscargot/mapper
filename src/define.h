/**
 * @file define.h
 * @author Liu Yu (source@liuyu.com)
 * @brief Common macros, structures, const values definition.
 * @version 1.0
 * @date 2019-10-07
 * 
 * @copyright Copyright (c) 2019
 * 
 */

#ifndef __MAPPER_DEFINE_H__
#define __MAPPER_DEFINE_H__

#include <assert.h>
#include <stdio.h>
#include <regex>
#include <string>
#include <sstream>

namespace mapper
{

static const int MAX_HOST_NAME = 253;
static const int DEFAULT_SESSIONS = 64;
static const int SESSION_TIMEOUT = 90;
static const int BUFFER_SIZE = 65536;

typedef struct MAP_DATA
{
    int port;
    char host[MAX_HOST_NAME + 1];
    int hostPort;

    MAP_DATA(int _port, const char *_host, int _hostPort) { init(_port, _host, _hostPort); }
    MAP_DATA(int _port, std::string _host, int _hostPort) { init(_port, _host.c_str(), _hostPort); }
    MAP_DATA(const MAP_DATA &src) { init(src.port, src.host, src.hostPort); }
    MAP_DATA &operator=(const MAP_DATA &src) { init(src.port, src.host, src.hostPort); }
    void init(int _port, std::string _host, int _hostPort) { init(_port, _host.c_str(), _hostPort); }
    void init(int _port, const char *_host, int _hostPort)
    {
        assert(0 <= _port && _port <= 65535 && _host && strlen(_host) && 0 <= _hostPort && _hostPort <= 65535);
        port = _port, snprintf(host, MAX_HOST_NAME + 1, "%s", _host), hostPort = _hostPort;
    }
    bool parse(std::string &dataEntry)
    {
        try
        {
            const char *REG_STRING = R"(^\s*\d{1,5}\s*\:\s*\S{1,253}\s*\:\s*\d{1,5}\s*$)";
            const char DELIM = ':';

            std::regex re(REG_STRING);
            std::smatch match;
            if (!std::regex_search(dataEntry, match, re))
            {
                return false;
            }

            std::size_t current, previous = 0;

            // port
            current = dataEntry.find(DELIM);
            int _port = atoi(dataEntry.substr(previous, current - previous).c_str());
            // host
            previous = current + 1;
            current = dataEntry.find(DELIM, previous);
            std::string _host = dataEntry.substr(previous, current - previous);
            // trim from start (in place)
            _host.erase(_host.begin(),
                        std::find_if(_host.begin(),
                                     _host.end(),
                                     [](int ch) {
                                         return !std::isspace(ch);
                                     }));

            // trim from end (in place)
            _host.erase(std::find_if(_host.rbegin(),
                                     _host.rend(),
                                     [](int ch) {
                                         return !std::isspace(ch);
                                     })
                            .base(),
                        _host.end());
            // hostPort
            previous = current + 1;
            int _hostPort = atoi(dataEntry.substr(previous).c_str());

            if (0 > _port || _port > 65535 || 0 > _hostPort || _hostPort > 65535)
            {
                return false;
            }

            init(_port, _host, _hostPort);

            return true;
        }
        catch (std::regex_error &e)
        {
            printf("[MAP_DATA::parse] catch an exception: [%s]", e.what());
        }
    }
    inline bool parse(const char *dataEntry)
    {
        std::string _dataEntry(dataEntry);
        return parse(_dataEntry);
    }
    std::string toStr()
    {
        std::stringstream ss;
        ss << port << ":" << host << ":" << hostPort;
        return ss.str();
    }
} MapData_t;

} // namespace mapper

#endif // __MAPPER_DEFINE_H__
