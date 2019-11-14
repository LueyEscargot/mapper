#ifndef __MAPPER_SESSION_H__
#define __MAPPER_SESSION_H__

#include <time.h>
#include <map>
#include <string>
#include "define.h"
#include "endpoint.h"
#include "session.h"
#include "timeoutContainer.h"
#include "buffer/buffer.h"

namespace mapper
{

class Session : public TimeoutContainer::Client
{
public:
    typedef enum STATE
    {
        INITIALIZED = 1,
        CONNECTING = 1 << 1,
        ESTABLISHED = 1 << 2,
        CLOSE = 1 << 3
    } State_t;

    using CB_JoinEpoll = std::function<bool(Endpoint *pEndpoint, bool read, bool write)>;
    using CB_SetEvents = std::function<bool(Endpoint *pEndpoint, bool read, bool write)>;
    using CB_StateChange_t = std::function<void(Session *pSession)>;

    Session(uint32_t bufSize);
    ~Session();

    void setStatus(State_t status);
    inline State_t getStatus() { return mStatus; }

    void init();
    bool init(int northSoc,
              int southSoc,
              CB_JoinEpoll cbJoinEpoll,
              CB_SetEvents cbSetEvents,
              CB_StateChange_t cbStateChange);
    inline bool valid() { return mNorthEndpoint.valid && mSouthEndpoint.valid; }
    std::string toStr();

    void onSoc(time_t curTime, Endpoint *pEndpoint, uint32_t events);
    void northSocRecv(time_t curTime);
    void northSocSend(time_t curTime);
    void southSocRecv(time_t curTime);
    void southSocSend(time_t curTime);

    Endpoint mNorthEndpoint;
    Endpoint mSouthEndpoint;

    Buffer *mpToNorthBuffer;
    Buffer *mpToSouthBuffer;

protected:
    State_t mStatus;
    static std::map<uint64_t, uint64_t> gCheckBox;

    CB_JoinEpoll mCbJoinEpoll;
    CB_SetEvents mCbSetEvents;
    CB_StateChange_t mCbStateChange;
};

} // namespace mapper

#endif // __MAPPER_SESSION_H__
