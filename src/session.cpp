#include "session.h"
#include <assert.h>
#include <errno.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <spdlog/spdlog.h>

using namespace std;

namespace mapper
{

Session::Session(uint32_t bufSize, int northSoc, int southSoc)
    : mpToNorthBuffer(nullptr),
      mpToSouthBuffer(nullptr),
      mNorthEndpoint(Endpoint::Type_t::NORTH, northSoc, this),
      mSouthEndpoint(Endpoint::Type_t::SOUTH, southSoc, this),
      mStatus(INITIALIZED)
{
    mpToNorthBuffer = Buffer::alloc(bufSize);
    if (!mpToNorthBuffer)
    {
        throw("create to north buffer fail");
    }
    mpToSouthBuffer = Buffer::alloc(bufSize);
    if (!mpToSouthBuffer)
    {
        throw("create to south buffer fail");
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

void Session::init(int northSoc, int southSoc)
{
    mpToNorthBuffer->init();
    mpToSouthBuffer->init();
    mNorthEndpoint.soc = northSoc;
    mSouthEndpoint.soc = southSoc;
    mNorthEndpoint.valid = true;
    mSouthEndpoint.valid = true;
    mStatus = State_t::INITIALIZED;
}

bool Session::onSoc(Endpoint *pEndpoint, uint32_t events, int epollfd)
{
    assert(pEndpoint->type & (Endpoint::Type_t::NORTH | Endpoint::Type_t::SOUTH));

    bool bRet;
    // recv
    if (events && EPOLLIN)
        if (pEndpoint->type == Endpoint::Type_t::NORTH)
            bRet = northSocRecv(epollfd);
        else
            bRet = southSocRecv(epollfd);

    // send
    if (events && EPOLLOUT)
        if (pEndpoint->type == Endpoint::Type_t::NORTH)
            bRet = northSocSend(epollfd) && bRet;
        else
            bRet = southSocSend(epollfd) && bRet;

    return bRet;
}

bool Session::northSocRecv(int epollfd)
{
    switch (mStatus)
    {
    case State_t::CONNECTING:
    case State_t::FAIL:
    case State_t::CLOSE:
        // 此状态下，不接收新数据
        return true;
    case State_t::ESTABLISHED:
        // for recv data
        break;
    default:
        spdlog::error("[Session::northSocRecv] invalid status: {}", mStatus);
        mStatus = State_t::FAIL;
        return false;
    }

    while (true)
    {
        uint64_t bufSize = mpToSouthBuffer->freeSize();
        if (bufSize == 0)
        {
            mpToSouthBuffer->stopRecv = true;
            // spdlog::trace("[Session::northSocRecv] to south buffer full");
            return southSocSend(epollfd);
        }

        char *buf = mpToSouthBuffer->getBuffer();
        int nRet = recv(mNorthEndpoint.soc, buf, bufSize, 0);
        if (nRet <= 0)
        {
            if (errno == EAGAIN || errno == EWOULDBLOCK)
            {
                return southSocSend(epollfd);
            }
            else if (nRet == 0)
            {
                // soc closed by peer
                mStatus = State_t::CLOSE;
                spdlog::debug("[Session::northSocRecv] sock[{}] closed by north peer", mNorthEndpoint.soc);
            }
            else
            {
                mStatus = State_t::FAIL;
                spdlog::error("[Session::northSocRecv] host sock[{}] recv fail: {}:[]",
                              mNorthEndpoint.soc, errno, strerror(errno));
            }

            mNorthEndpoint.valid = false;
            // send last data to south
            southSocSend(epollfd);
            return false;
        }
        else
        {
            mpToSouthBuffer->incDataSize(nRet);
        }
    }

    return true;
}

bool Session::northSocSend(int epollfd)
{
    switch (mStatus)
    {
    case State_t::CONNECTING:
        // 会话已建立，更改当前状态，并允许 北向及南向 soc 收发数据
        {
            // North Soc
            {
                assert(mNorthEndpoint.check());
                if (!resetEpollMode(epollfd, mNorthEndpoint.soc,
                                    EPOLLIN | EPOLLOUT | EPOLLET | EPOLLRDHUP, &mNorthEndpoint))
                {
                    spdlog::error("[Session::northSocSend] Failed to modify north soc[{}] in epoll. Error{}: {}",
                                  mNorthEndpoint.soc, errno, strerror(errno));
                    mNorthEndpoint.valid = false;
                    mSouthEndpoint.valid = false;
                    mStatus = State_t::CLOSE;
                    return false;
                }
            }
            // South Soc
            {
                assert(mSouthEndpoint.check());
                if (!resetEpollMode(epollfd, mSouthEndpoint.soc,
                                    EPOLLIN | EPOLLOUT | EPOLLET | EPOLLRDHUP, &mSouthEndpoint))
                {
                    spdlog::error("[Session::northSocSend] Failed to modify south soc[{}] in epoll. Error{}: {}",
                                  mSouthEndpoint.soc, errno, strerror(errno));
                    mNorthEndpoint.valid = false;
                    mSouthEndpoint.valid = false;
                    mStatus = State_t::CLOSE;
                    return false;
                }
            }

            spdlog::debug("[Session::northSocSend] session[{}-{}] established.",
                          mSouthEndpoint.soc, mNorthEndpoint.soc);
            mStatus = State_t::ESTABLISHED;

            // 继续进行后续数据发送操作
            break;
        }
    case State_t::FAIL:
    case State_t::CLOSE:
        if (mNorthEndpoint.valid)
        {
            // 继续进行后续数据发送操作
            break;
        }
        else
        {
            return false;
        }
    case State_t::ESTABLISHED:
        // 继续进行后续数据发送操作
        break;
    default:
        spdlog::error("[Session::northSocSend] invalid status: {}", mStatus);
        return false;
    }

    bool bRet = true;
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
                // send action finished
                break;
            }

            spdlog::error("[Session::northSocSend] sock[{}] send fail: {} - []",
                          mNorthEndpoint.soc, errno, strerror(errno));
            mNorthEndpoint.valid = false;
            if (mStatus == State_t::ESTABLISHED)
            {
                mStatus = State_t::FAIL;
            }
            bRet = false;
            break;
        }

        mpToNorthBuffer->incFreeSize(nRet);
    }

    // 判断是否已有能力接收从南向来的数据
    if (mpToNorthBuffer->stopRecv && mpToNorthBuffer->freeSize() && mSouthEndpoint.valid)
    {
        mpToNorthBuffer->stopRecv = false;

        if (!resetEpollMode(epollfd, mSouthEndpoint.soc,
                            EPOLLIN | EPOLLOUT | EPOLLET | EPOLLRDHUP,
                            &mSouthEndpoint))
        {
            spdlog::error("[Session::northSocSend] reset north soc[{}] fail", mSouthEndpoint.soc);
            mSouthEndpoint.valid = false;
            bRet = false;
        }
    }

    // 1) 如果是 CLOSE, FAIL 这两种状态且数据发送完毕；2）发生异常之后：直接关闭会话
    if (mStatus & (State_t::CLOSE | State_t::FAIL) && mpToSouthBuffer->empty() ||
        !mSouthEndpoint.valid)
    {
        // 通过返回 false 关闭会话。
        mNorthEndpoint.valid = false;
        bRet = false;
    }

    return bRet;
}

bool Session::southSocRecv(int epollfd)
{
    switch (mStatus)
    {
    case State_t::CONNECTING:
    case State_t::FAIL:
    case State_t::CLOSE:
        // 此状态下，不接收新数据
        return true;
    case State_t::ESTABLISHED:
        // for recv data
        break;
    default:
        spdlog::error("[Session::southSocRecv] invalid status: {}", mStatus);
        mStatus = State_t::FAIL;
        return false;
    }

    while (true)
    {
        uint64_t bufSize = mpToNorthBuffer->freeSize();
        if (bufSize == 0)
        {
            mpToNorthBuffer->stopRecv = true;
            // spdlog::trace("[Session::southSocRecv] to south buffer full");
            return northSocSend(epollfd); // 0: northSocSend 只在 CONNECTING 阶段需要 epollfd。
        }

        char *buf = mpToNorthBuffer->getBuffer();
        int nRet = recv(mSouthEndpoint.soc, buf, bufSize, 0);
        if (nRet <= 0)
        {
            if (errno == EAGAIN || errno == EWOULDBLOCK)
            {
                return northSocSend(epollfd); // 0: northSocSend 只在 CONNECTING 阶段需要 epollfd。
            }
            else if (nRet == 0)
            {
                // soc closed by peer
                mStatus = State_t::CLOSE;
                spdlog::debug("[Session::southSocRecv] sock[{}] closed by south peer", mSouthEndpoint.soc);
            }
            else
            {
                mStatus = State_t::FAIL;
                spdlog::error("[Session::southSocRecv] south sock[{}] recv fail: {}:[]",
                              mSouthEndpoint.soc, errno, strerror(errno));
            }

            mSouthEndpoint.valid = false;
            // send last data to south
            northSocSend(epollfd); // 0: northSocSend 只在 CONNECTING 阶段需要 epollfd。
            return false;
        }
        else
        {
            mpToNorthBuffer->incDataSize(nRet);
        }
    }

    return true;
}

bool Session::southSocSend(int epollfd)
{
    switch (mStatus)
    {
    case State_t::FAIL:
    case State_t::CLOSE:
        if (mSouthEndpoint.valid)
        {
            // 如果到客户端（南向）没问题，那服务器（北向）接口的接口必然已经出错
            assert(!mNorthEndpoint.valid);

            // 继续进行后续数据发送操作
            break;
        }
        else
        {
            return false;
        }
    case State_t::ESTABLISHED:
        // 继续进行后续数据发送操作
        break;
    case State_t::CONNECTING:
        // South Socket 不会进入此状态
    default:
        spdlog::error("[Session::southSocSend] invalid status: {}", mStatus);
        return false;
    }

    bool bRet = true;
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
                // send action finished
                break;
            }

            spdlog::error("[Session::southSocSend] sock[{}] send fail: {} - []",
                          mSouthEndpoint.soc, errno, strerror(errno));
            mSouthEndpoint.valid = false;
            if (mStatus == State_t::ESTABLISHED)
            {
                mStatus = State_t::FAIL;
            }
            bRet = false;
            break;
        }

        mpToSouthBuffer->incFreeSize(nRet);
    }

    // 判断是否已有能力接收从北向来的数据
    if (mpToSouthBuffer->stopRecv && mpToSouthBuffer->freeSize() && mNorthEndpoint.valid)
    {
        mpToSouthBuffer->stopRecv = false;

        if (!resetEpollMode(epollfd, mNorthEndpoint.soc,
                            EPOLLIN | EPOLLOUT | EPOLLET | EPOLLRDHUP,
                            &mNorthEndpoint))
        {
            spdlog::error("[Session::southSocSend] reset north soc[{}] fail", mNorthEndpoint.soc);
            mNorthEndpoint.valid = false;
            bRet = false;
        }
    }

    // 1) 如果是 CLOSE, FAIL 这两种状态且数据发送完毕；2）发生异常之后：直接关闭会话
    if (mStatus & (State_t::CLOSE | State_t::FAIL) && mpToSouthBuffer->empty() ||
        !mSouthEndpoint.valid)
    {
        // 通过返回 false 关闭会话。
        mSouthEndpoint.valid = false;
        bRet = false;
    }

    return bRet;
}

bool Session::resetEpollMode(int epollfd, int soc, uint32_t mode, void *tag)
{
    assert(epollfd && soc);
    // spdlog::trace("[Session::resetEpollMode] soc[{}], mode[{}], tag[{}]", soc, mode, tag);
    epoll_event event;
    event.events = mode;
    event.data.ptr = tag;
    if (epoll_ctl(epollfd, EPOLL_CTL_MOD, soc, &event))
    {
        spdlog::error("[Session::resetEpollMode] Reset mode[{}] for soc[{}]@epoll[{}] fail. Error {}: {}",
                      mode, soc, epollfd, errno, strerror(errno));
        return false;
    }

    return true;
}

} // namespace mapper
