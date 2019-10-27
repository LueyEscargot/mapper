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
static const int DEFAULT_SESSIONS = 64;
static const int SESSION_TIMEOUT = 90;
static const int BUFFER_SIZE = 4096;

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

typedef struct SOCK_BASE
{
    SockType_t type;
    int soc;

    SOCK_BASE(SockType_t _type, int _soc)
        : type(_type), soc(_soc) {}
    void init(SockType_t _type) { type = _type, init(0); }
    void init(int _soc) { soc = _soc; }
} SockBase_t;

typedef struct SOCK_SVR : SOCK_BASE
{
    MAP_DATA mapData;

    SOCK_SVR(int soc, int port, const char *host, int hostPort)
        : SOCK_BASE(SOCK_TYPE::SVR_SOCK, soc),
          mapData(port, host, hostPort) {}
} SockSvr_t;

struct SOCK_HOST;
struct SESSION;

typedef struct SOCK_CLIENT : SOCK_BASE
{
    SOCK_HOST *pHostSock;
    SESSION *pSession;

    void init(SESSION *_pSession, SOCK_HOST *_pHostSock)
    {
        SOCK_BASE::init(SOCK_TYPE::CLIENT_SOCK);
        pSession = _pSession;
        pHostSock = _pHostSock;
    }
    void init(int _soc)
    {
        SOCK_BASE::init(_soc);
    }
} SockClient_t;

typedef struct SOCK_HOST : SOCK_BASE
{
    SOCK_CLIENT *pClientSock;
    SESSION *pSession;

    void init(SESSION *_pSession, SOCK_CLIENT *_pClientSock)
    {
        SOCK_BASE::init(SOCK_TYPE::HOST_SOCK);
        pSession = _pSession;
        pClientSock = _pClientSock;
    }
    void init(int _soc)
    {
        SOCK_BASE::init(_soc);
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
    bool toClientSockFail;
    bool toHostSockFail;
    Buffer<BUFFER_SIZE> buffer2Client;
    Buffer<BUFFER_SIZE> buffer2Host;
    bool fullFlag2Client;
    bool fullFlag2Host;

    void init()
    {
        clientSoc.init(this, &hostSoc);
        hostSoc.init(this, &clientSoc);
    }
    void init(int _clientSoc, int _hostSoc, StateMachine_t _status = STATE_MACHINE::CONNECTING)
    {
        clientSoc.init(_clientSoc);
        hostSoc.init(_hostSoc);
        lastAccessTime = 0;
        status = _status;
        toClientSockFail = false;
        toHostSockFail = false;
        buffer2Client.init();
        buffer2Host.init();
        fullFlag2Client = false;
        fullFlag2Host = false;
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
