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
#define __MAPPER_DEFINE_H__

#include <mutex>
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

typedef struct SESSION
{
    int clientSoc;
    int hostSoc;
    int64_t lastAccessTime;
    StateMachine_t status;

    std::mutex sessionMutex;

    SessionBuffer<BUFFER_SIZE, BUFFER_SIZE> toHostBuffer;
    SessionBuffer<BUFFER_SIZE, BUFFER_SIZE> toClientBuffer;
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
