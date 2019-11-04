#ifndef __MAPPER_SESSION_H__
#define __MAPPER_SESSION_H__

#include "buffer.h"
#include "define.h"
#include "endpoint.h"
#include "session.h"

namespace mapper
{

class Session
{
public:
    typedef enum STATE
    {
        INITIALIZED = 1,
        CONNECTING = 1 << 1,
        ESTABLISHED = 1 << 2,
        CLOSE = 1 << 3,
        FAIL = 1 << 4
    } State_t;

    Session(uint32_t bufSize, int northSoc, int southSoc);
    ~Session();

    bool onSoc(Endpoint *pEndpoint, uint32_t events, int epollfd);
    bool northSocRecv(int epollfd);
    bool northSocSend(int epollfd);
    bool southSocRecv(int epollfd);
    bool southSocSend(int epollfd);

    Endpoint mNorthEndpoint;
    Endpoint mSouthEndpoint;

    Buffer *mpToNorthBuffer;
    Buffer *mpToSouthBuffer;

    State_t mStatus;

protected:
    bool resetEpollMode(int epollfd, int soc, uint32_t mode, void *tag);
};

} // namespace mapper

#endif // __MAPPER_SESSION_H__
