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

#include "sessionBuffer.hpp"

namespace mapper
{

static const int MAX_SESSIONS = 10240;
static const int SESSION_TIMEOUT = 90;
static const int BUFFER_SIZE = 1024;

typedef enum STATE_MACHINE
{
    INIT = 0,
    CONNECTING,
    CONNECTED,
    CLOSE,
    FAIL
} StateMachine_t;

typedef enum SOCK_TYPE
{
    CLIENT_SOCK = 0,
    HOST_SOCK
} SockType_t;

struct SESSION;

typedef struct SOCK
{
    int soc;
    int events;
    SockType_t type;
    SESSION *pSession;

    void init(SockType_t _type, SESSION *_pSession, int _soc = 0, int _events = 0)
    {
        type = _type;
        pSession = _pSession;

        soc = _soc;
        events = _events;
    }
} Sock_t;

typedef struct SESSION
{
    Sock_t clientSoc;
    Sock_t hostSoc;
    int64_t lastAccessTime;
    StateMachine_t status;

    SessionBuffer<BUFFER_SIZE, BUFFER_SIZE> buffer;

    void init(int _clientSoc, int _clientEvents, int _hostSoc = 0, int _hostEvents = 0)
    {
        clientSoc.init(SOCK_TYPE::CLIENT_SOCK, this, _clientSoc, _clientEvents);
        hostSoc.init(SOCK_TYPE::HOST_SOCK, this, _hostSoc, _hostEvents);
        int64_t lastAccessTime = 0;
        StateMachine_t status = STATE_MACHINE::INIT;
        buffer.init();
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
