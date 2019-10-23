#ifndef __MAPPER_SESSION_H__
#define __MAPPER_SESSION_H__

#include "define.h"

namespace mapper
{

class Session
{
protected:
    Session() = default;
    ~Session() = default;

public:
    static bool onClientSockRecv(Session_t *pSession);
    static bool onHostSockRecv(Session_t *pSession);
    static bool onClientSockSend(Session_t *pSession);
    static bool onHostSockSend(Session_t *pSession, int epollfd);

protected:
    static bool recvFromClient(Session_t *pSession);
    static bool recvFromHost(Session_t *pSession);
    static bool sendToClient(Session_t * pSession);
    static bool sendToHost(Session_t * pSession);
};

} // namespace mapper

#endif // __MAPPER_SESSION_H__
