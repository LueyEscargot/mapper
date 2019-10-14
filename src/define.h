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
#include "buffer.hpp"

namespace mapper
{

static const int MAX_HOST_NAME = 253;
static const int MAX_SESSIONS = 10240;
static const int SESSION_TIMEOUT = 90;
static const int BUFFER_SIZE = 1024;

typedef enum STATE_MACHINE
{
    INITIALIZED = 0,
    CONNECTING,
    ESTABLISHED,
    CLOSE,
    FAIL
} StateMachine_t;

typedef enum SOCK_TYPE
{
    SVR_SOCK = 0,
    CLIENT_SOCK,
    HOST_SOCK
} SockType_t;

typedef struct MAP_DATA
{
    int port;
    char target[MAX_HOST_NAME + 1];
    int targetPort;

    MAP_DATA(int _port, const char *_target, int _targetPort) { init(_port, _target, _targetPort); }
    MAP_DATA(int _port, std::string _target, int _targetPort) { init(_port, _target.c_str(), _targetPort); }
    MAP_DATA(const MAP_DATA &src) { init(src.port, src.target, src.targetPort); }
    MAP_DATA &operator=(const MAP_DATA &src) { init(src.port, src.target, src.targetPort); }
    void init(int _port, std::string _target, int _targetPort) { init(_port, _target.c_str(), _targetPort); }
    void init(int _port, const char *_target, int _targetPort)
    {
        assert(0 <= _port && _port <= 65535 && _target && strlen(_target) && 0 <= _targetPort && _targetPort <= 65535);
        port = _port, snprintf(target, MAX_HOST_NAME + 1, "%s", _target), targetPort = _targetPort;
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
                // spdlog::debug("[struct MAP_DATA] invlaid map data: [{}]", dataEntry);
                return false;
            }

            std::size_t current, previous = 0;

            // port
            current = dataEntry.find(DELIM);
            int _port = atoi(dataEntry.substr(previous, current - previous).c_str());
            // target
            previous = current + 1;
            current = dataEntry.find(DELIM, previous);
            std::string _target = dataEntry.substr(previous, current - previous);
            // trim from start (in place)
            _target.erase(_target.begin(),
                          std::find_if(_target.begin(),
                                       _target.end(),
                                       [](int ch) {
                                           return !std::isspace(ch);
                                       }));

            // trim from end (in place)
            _target.erase(std::find_if(_target.rbegin(),
                                       _target.rend(),
                                       [](int ch) {
                                           return !std::isspace(ch);
                                       })
                              .base(),
                          _target.end());
            // targetPort
            previous = current + 1;
            int _targetPort = atoi(dataEntry.substr(previous).c_str());

            if (0 > _port || _port > 65535 || 0 > _targetPort || _targetPort > 65535)
            {
                // spdlog::debug("[struct MAP_DATA] invlaid config data: [{}]", dataEntry);
                return false;
            }

            init(_port, _target, _targetPort);

            return true;
        }
        catch (std::regex_error &e)
        {
            // spdlog::error("[struct MAP_DATA] catch an exception: [{}]", e);
        }
    }
    inline bool parse(const char *dataEntry)
    {
        std::string _dataEntry(dataEntry);
        return parse(_dataEntry);
    }
    std::string toString()
    {
        std::stringstream ss;
        ss << port << ":" << target << ":" << targetPort;
        return ss.str();
    }
} MapData_t;

typedef struct SOCK_BASE
{
    SockType_t type;
    int soc;
    int events;

    SOCK_BASE(SockType_t _type, int _soc, int _events = 0)
        : type(_type), soc(_soc), events(_events) {}
    void init(SockType_t _type) { type = _type, init(0, 0); }
    void init(int _soc, int _events) { soc = _soc, events = _events; }
} SockBase_t;

typedef struct SOCK_SVR : SOCK_BASE
{
    MAP_DATA mapData;

    SOCK_SVR(int soc, int port, const char *target, int targetPort)
        : SOCK_BASE(SOCK_TYPE::SVR_SOCK, soc),
          mapData(port, target, targetPort) {}
} SockSvr_t;

struct SOCK_HOST;
struct SESSION;

typedef struct SOCK_CLIENT : SOCK_BASE
{
    Buffer<BUFFER_SIZE> buffer;
    bool fullFlag;
    SOCK_HOST *pHostSock;
    SESSION *pSession;

    void init(SESSION *_pSession, SOCK_HOST *_pHostSock)
    {
        SOCK_BASE::init(SOCK_TYPE::CLIENT_SOCK);
        pSession = _pSession;
        pHostSock = _pHostSock;
    }
    void init(int _soc, int _events)
    {
        SOCK_BASE::init(_soc, _events);
        fullFlag = false;
        buffer.init();
    }
} SockClient_t;

typedef struct SOCK_HOST : SOCK_BASE
{
    Buffer<BUFFER_SIZE> buffer;
    bool fullFlag;
    SOCK_CLIENT *pClientSock;
    SESSION *pSession;

    void init(SESSION *_pSession, SOCK_CLIENT *_pClientSock)
    {
        SOCK_BASE::init(SOCK_TYPE::HOST_SOCK);
        pSession = _pSession;
        pClientSock = _pClientSock;
    }
    void init(int _soc, int _events)
    {
        SOCK_BASE::init(_soc, _events);
        fullFlag = false;
        buffer.init();
    }
} SockHost_t;

typedef struct SOCK
{
    union {
        SOCK_SVR svrSock;
        SOCK_CLIENT clientSock;
        SOCK_HOST hostSock;
    };
} Sock_t;

typedef struct SESSION
{
    SockClient_t clientSoc;
    SockHost_t hostSoc;
    int64_t lastAccessTime;
    StateMachine_t status;

    void init()
    {
        clientSoc.init(this, &hostSoc);
        hostSoc.init(this, &clientSoc);
    }
    void init(int _clientSoc, int _clientEvents, int _hostSoc = 0, int _hostEvents = 0)
    {
        clientSoc.init(_clientSoc, _clientEvents);
        hostSoc.init(_hostSoc, _hostEvents);
        int64_t lastAccessTime = 0;
        StateMachine_t status = STATE_MACHINE::INITIALIZED;
    }
} Session_t;

typedef struct FREE_ITEM
{
    union {
        FREE_ITEM *next;
        Session_t session;
    };
} FreeItem_t;

} // namespace mapper

#endif // __MAPPER_DEFINE_H__
