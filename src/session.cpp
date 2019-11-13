#include "session.h"
#include <assert.h>
#include <errno.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <map>
#include <spdlog/spdlog.h>

using namespace std;
using namespace spdlog;

namespace mapper
{

/**
 * @brief Status Change Rules
 * 
 *              INITIALIZED CONNECTING   ESTABLISHED    CLOSE
 *  INITIALIZED      -          X             X           V
 *  CONNECTING       V          -             X           X
 *  ESTABLISHED      X          V             -           X
 *  CLOSE            X          V             V           - 
 */
map<uint64_t, uint64_t> Session::gCheckBox = {
    {State_t::INITIALIZED, State_t::CLOSE},
    {State_t::CONNECTING, State_t::INITIALIZED},
    {State_t::ESTABLISHED, State_t::CONNECTING},
    {State_t::CLOSE, (State_t::CONNECTING | State_t::ESTABLISHED)}};

Session::Session(uint32_t bufSize)
    : mpToNorthBuffer(nullptr),
      mpToSouthBuffer(nullptr),
      mNorthEndpoint(Endpoint::Type_t::NORTH, 0, this),
      mSouthEndpoint(Endpoint::Type_t::SOUTH, 0, this),
      mStatus(INITIALIZED)
{
    mpToNorthBuffer = Buffer::alloc(bufSize);
    mpToSouthBuffer = Buffer::alloc(bufSize);
    if (!mpToNorthBuffer || !mpToSouthBuffer)
    {
        throw("[Session::Session] create buffer fail");
    }
}

Session::~Session()
{
    spdlog::trace("[Session::~Session] session[{}] with endpoints[{}_{}:{}_{}] released",
                  (void *)this,
                  mSouthEndpoint.soc, (void *)&mSouthEndpoint,
                  mNorthEndpoint.soc, (void *)&mNorthEndpoint);
    Buffer::release(mpToNorthBuffer);
    Buffer::release(mpToSouthBuffer);
    mpToNorthBuffer = nullptr;
    mpToSouthBuffer = nullptr;
}

void Session::setStatus(State_t status)
{
    if (mStatus == status)
    {
        return;
    }

    if (!(gCheckBox[status] & mStatus))
    {
        spdlog::error("[Session::setStatus] change status fail: {} --x--> {}", mStatus, status);
        return;
    }

    switch (status)
    {
    case State_t::INITIALIZED:
        mStatus = status;
        return;
    case CONNECTING:
        if (mCbJoinEpoll(&mNorthEndpoint, false, true) &&
            mCbJoinEpoll(&mSouthEndpoint, false, false))
        {
            mStatus = status;
        }
        else
        {
            spdlog::error("[Session::setStatus] reset epoll mode fail, set connecting session to close");
            return setStatus(State_t::CLOSE);
        }
        break;
    case ESTABLISHED:
        // 会话已建立，更改当前状态，并允许 北向及南向 soc 收发数据
        if (mCbSetEvents(&mNorthEndpoint, true, true) &&
            mCbSetEvents(&mSouthEndpoint, true, true))
        {
            mStatus = status;
            spdlog::debug("[Session::northSocSend] session[{}-{}] established.",
                          mSouthEndpoint.soc, mNorthEndpoint.soc);
        }
        else
        {
            spdlog::error("[Session::setStatus] reset epoll mode fail, set session to close");
            return setStatus(State_t::CLOSE);
        }
        break;
    case CLOSE:
        if (!mCbSetEvents(&mNorthEndpoint, false, mNorthEndpoint.valid) ||
            !mCbSetEvents(&mSouthEndpoint, false, mSouthEndpoint.valid))
        {
            spdlog::error("[Session::setStatus] reset epoll mode fail at close mode");
            mSouthEndpoint.valid = false;
            mSouthEndpoint.valid = false;
        }
        mStatus = status;
        break;
    default:
        spdlog::critical("[Session::setStatus] invalid status: {}", mStatus);
        assert(false);
    }

    mCbStateChange(this);
}

void Session::init()
{
    mpToNorthBuffer->init();
    mpToSouthBuffer->init();
    mNorthEndpoint.soc = 0;
    mSouthEndpoint.soc = 0;
    mNorthEndpoint.valid = true;
    mSouthEndpoint.valid = true;

    mCbJoinEpoll = nullptr;
    mCbSetEvents = nullptr;
    mCbStateChange = nullptr;

    setStatus(State_t::INITIALIZED);
}

bool Session::init(int northSoc,
                   int southSoc,
                   CB_JoinEpoll cbJoinEpoll,
                   CB_SetEvents cbSetEvents,
                   CB_StateChange_t cbStateChange)
{
    mNorthEndpoint.soc = northSoc;
    mSouthEndpoint.soc = southSoc;

    mCbJoinEpoll = cbJoinEpoll;
    mCbSetEvents = cbSetEvents;
    mCbStateChange = cbStateChange;

    setStatus(State_t::CONNECTING);

    return getStatus() != State_t::CLOSE;
}

string Session::toStr()
{
    stringstream ss;

    ss << this
       << "("
       << mNorthEndpoint.soc
       << (mNorthEndpoint.valid ? "_:" : "x:")
       << mSouthEndpoint.soc
       << (mSouthEndpoint.valid ? "_:" : "x:")
       << (mpToNorthBuffer ? mpToNorthBuffer->dataSize() : 0)
       << ":"
       << (mpToSouthBuffer ? mpToSouthBuffer->dataSize() : 0)
       << ")";

    return ss.str();
}

void Session::onSoc(Endpoint *pEndpoint, uint32_t events)
{
    assert(pEndpoint->type & (Endpoint::Type_t::NORTH | Endpoint::Type_t::SOUTH));

    // recv
    if (events && EPOLLIN)
        if (pEndpoint->type == Endpoint::Type_t::NORTH)
            northSocRecv();
        else
            southSocRecv();

    // send
    if (events && EPOLLOUT)
        if (pEndpoint->type == Endpoint::Type_t::NORTH)
            northSocSend();
        else
            southSocSend();
}

void Session::northSocRecv()
{
    switch (mStatus)
    {
    case State_t::CONNECTING:
    case State_t::CLOSE:
        // 此状态下，不接收新数据
        return;
    case State_t::ESTABLISHED:
        // for recv data
        break;
    default:
        spdlog::error("[Session::northSocRecv] invalid status: {}", mStatus);
        setStatus(State_t::CLOSE);
        return;
    }

    while (true)
    {
        uint64_t bufSize = mpToSouthBuffer->freeSize();
        if (bufSize == 0)
        {
            mpToSouthBuffer->stopRecv = true;
            // spdlog::trace("[Session::northSocRecv] to south buffer full");
            southSocSend();

            // try again
            bufSize = mpToSouthBuffer->freeSize();
            if (bufSize == 0)
            {
                break;
            }
        }

        char *buf = mpToSouthBuffer->getBuffer();
        int nRet = recv(mNorthEndpoint.soc, buf, bufSize, 0);
        if (nRet <= 0)
        {
            if (errno == EAGAIN || errno == EWOULDBLOCK)
            {
                // 此次数据接收已完毕，先尝试发送缓冲区中的数据
                southSocSend();
            }
            else
            {
                if (nRet == 0)
                {
                    // soc closed by peer
                    spdlog::debug("[Session::northSocRecv] sock[{}] closed by north peer", mNorthEndpoint.soc);
                }
                else
                {
                    spdlog::error("[Session::northSocRecv] host sock[{}] recv fail: {}:[]",
                                  mNorthEndpoint.soc, errno, strerror(errno));
                }

                mNorthEndpoint.valid = false;
                // 设定状态为关闭前先尝试发送剩余数据
                southSocSend();
                setStatus(State_t::CLOSE);
            }

            break;
        }
        else
        {
            mpToSouthBuffer->incDataSize(nRet);
        }
    }
}

void Session::northSocSend()
{
    switch (mStatus)
    {
    case State_t::CONNECTING:
        // 会话已建立
        setStatus(State_t::ESTABLISHED);
        return;
    case State_t::ESTABLISHED:
    case State_t::CLOSE:
        if (!mNorthEndpoint.valid)
        {
            return;
        }
        break;
    default:
        spdlog::error("[Session::northSocSend] invalid status: {}", mStatus);
        setStatus(State_t::CLOSE);
        return;
    }

    uint64_t bufSize;
    while (bufSize = mpToNorthBuffer->dataSize())
    {
        // send data to north
        char *buf = mpToNorthBuffer->getData();
        int nRet = send(mNorthEndpoint.soc, buf, bufSize, 0);
        if (nRet < 0)
        {
            if (errno == EAGAIN || errno == EWOULDBLOCK)
            {
                // 此次发送窗口已关闭
                break;
            }

            spdlog::error("[Session::northSocSend] sock[{}] send fail: {} - []",
                          mNorthEndpoint.soc, errno, strerror(errno));
            mNorthEndpoint.valid = false;
            setStatus(State_t::CLOSE);
            break;
        }

        mpToNorthBuffer->incFreeSize(nRet);
    }

    // 判断是否已有能力接收从南向来的数据
    if (getStatus() != State_t::CLOSE &&
        mpToNorthBuffer->stopRecv &&
        mpToNorthBuffer->freeSize() &&
        mSouthEndpoint.valid)
    {
        mpToNorthBuffer->stopRecv = false;
        if (!mCbSetEvents(&mSouthEndpoint, true, true))
        {
            spdlog::error("[Session::northSocSend] reset north soc[{}] fail", mSouthEndpoint.soc);
            setStatus(State_t::CLOSE);
        }
    }
}

void Session::southSocRecv()
{
    switch (mStatus)
    {
    case State_t::CONNECTING:
    case State_t::CLOSE:
        // 此状态下，不接收新数据
        return;
    case State_t::ESTABLISHED:
        break;
    default:
        spdlog::error("[Session::southSocRecv] invalid status: {}", mStatus);
        setStatus(State_t::CLOSE);
        return;
    }

    while (true)
    {
        uint64_t bufSize = mpToNorthBuffer->freeSize();
        if (bufSize == 0)
        {
            mpToNorthBuffer->stopRecv = true;
            // spdlog::trace("[Session::southSocRecv] to south buffer full");
            northSocSend();

            // try again
            bufSize = mpToSouthBuffer->freeSize();
            if (bufSize == 0)
            {
                break;
            }
        }

        char *buf = mpToNorthBuffer->getBuffer();
        int nRet = recv(mSouthEndpoint.soc, buf, bufSize, 0);
        if (nRet <= 0)
        {
            if (errno == EAGAIN || errno == EWOULDBLOCK)
            {
                // 此次数据接收已完毕，先尝试发送缓冲区中的数据
                northSocSend();
            }
            else
            {
                if (nRet == 0)
                {
                    // soc closed by peer
                    spdlog::debug("[Session::southSocRecv] sock[{}] closed by south peer", mSouthEndpoint.soc);
                }
                else
                {
                    spdlog::error("[Session::southSocRecv] south sock[{}] recv fail: {}:[]",
                                  mSouthEndpoint.soc, errno, strerror(errno));
                }

                // 设定状态为关闭前先尝试发送剩余数据
                mSouthEndpoint.valid = false;
                northSocSend();
                setStatus(State_t::CLOSE);
            }

            break;
        }
        else
        {
            mpToNorthBuffer->incDataSize(nRet);
        }
    }
}

void Session::southSocSend()
{
    switch (mStatus)
    {
    case State_t::ESTABLISHED:
        break;
    case State_t::CLOSE:
        if (!mSouthEndpoint.valid)
        {
            return;
        }
        break;
    default:
        spdlog::error("[Session::southSocSend] invalid status: {}", mStatus);
        setStatus(State_t::CLOSE);
        return;
    }

    uint64_t bufSize;
    while (bufSize = mpToSouthBuffer->dataSize())
    {
        // send data to south
        char *buf = mpToSouthBuffer->getData();
        int nRet = send(mSouthEndpoint.soc, buf, bufSize, 0);
        if (nRet < 0)
        {
            if (errno == EAGAIN || errno == EWOULDBLOCK)
            {
                // 此次发送窗口已关闭
                break;
            }

            spdlog::error("[Session::southSocSend] sock[{}] send fail: {} - []",
                          mSouthEndpoint.soc, errno, strerror(errno));
            mSouthEndpoint.valid = false;
            setStatus(State_t::CLOSE);
            break;
        }

        mpToSouthBuffer->incFreeSize(nRet);
    }

    // 判断是否已有能力接收从北向来的数据
    if (getStatus() != State_t::CLOSE &&
        mpToSouthBuffer->stopRecv &&
        mpToSouthBuffer->freeSize() &&
        mNorthEndpoint.valid)
    {
        mpToSouthBuffer->stopRecv = false;
        if (!mCbSetEvents(&mNorthEndpoint, true, true))
        {
            spdlog::error("[Session::southSocSend] reset north soc[{}] fail", mNorthEndpoint.soc);
            setStatus(State_t::CLOSE);
        }
    }
}

} // namespace mapper
