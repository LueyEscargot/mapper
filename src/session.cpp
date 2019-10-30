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

bool Session::onClientSockRecv(Session_t *pSession)
{
    switch (pSession->status)
    {
    case STATE_MACHINE::CONNECTING:
    case STATE_MACHINE::FAIL:
        // Do nothing
        return true;
    case STATE_MACHINE::ESTABLISHED:
        return recvFromClient(pSession);
    default:
        spdlog::error("[Session::onClientSockRecv] invalid status: {}", pSession->status);
        return false;
    }

    return true;
}

bool Session::onHostSockRecv(Session_t *pSession)
{
    switch (pSession->status)
    {
    case STATE_MACHINE::CONNECTING:
    case STATE_MACHINE::FAIL:
        // Do nothing
        return true;
    case STATE_MACHINE::ESTABLISHED:
        return recvFromHost(pSession);
    default:
        spdlog::error("[Session::onHostSockRecv] invalid status: {}", pSession->status);
        return false;
    }

    return true;
}

bool Session::onClientSockSend(Session_t *pSession)
{
    switch (pSession->status)
    {
    case STATE_MACHINE::CONNECTING:
        // Do nothing
        return true;
    case STATE_MACHINE::ESTABLISHED:
        return sendToClient(pSession);
    case STATE_MACHINE::FAIL:
        if (pSession->toClientSockFail || pSession->buffer2Client.empty())
        {
            // Do nothing
            return true;
        }
        else
        {
            // 如果客户端接口没问题，那到服务器的接口必然已经出错
            assert(pSession->toHostSockFail);

            if (!sendToClient(pSession))
            {
                spdlog::debug("[Session::onClientSockSend] send to client[{}] fail.",
                              pSession->clientSoc.soc);
                pSession->toClientSockFail = true;
                return false;
            }
            else if (pSession->buffer2Client.empty())
            {
                spdlog::debug("[Session::onClientSockSend] last data to client[{}] has been sent.",
                              pSession->clientSoc.soc);
                pSession->toClientSockFail = true;
                return false;
            }
            else
            {
                return true;
            }
        }
    default:
        spdlog::error("[Session::onClientSockSend] invalid status: {}", pSession->status);
        return false;
    }

    return true;
}

bool Session::onHostSockSend(Session_t *pSession, int epollfd)
{
    switch (pSession->status)
    {
    case STATE_MACHINE::CONNECTING:
        // 会话已建立，更改当前状态，并允许客户端 soc 读取数据
        {
            SockClient_t *pClientSoc = &pSession->clientSoc;
            struct epoll_event event;
            event.data.ptr = pClientSoc;
            event.events = EPOLLIN | EPOLLOUT | EPOLLET | EPOLLRDHUP;
            if (epoll_ctl(epollfd, EPOLL_CTL_MOD, pClientSoc->soc, &event))
            {
                spdlog::error("[Session::onHostSockRecv] Failed to modify client soc[{}] in epoll. Error{}: {}",
                              pClientSoc->soc, errno, strerror(errno));
                pSession->status == STATE_MACHINE::FAIL;
                pSession->toHostSockFail = true;
                pSession->toClientSockFail = true;
                return false;
            }

            pSession->status = STATE_MACHINE::ESTABLISHED;
            spdlog::debug("[Session::onHostSockRecv] session[{}-{}] established.",
                          pSession->clientSoc.soc, pSession->hostSoc.soc);
        }
        return recvFromHost(pSession);
    case STATE_MACHINE::ESTABLISHED:
        return sendToHost(pSession);
    case STATE_MACHINE::FAIL:
        if (pSession->toHostSockFail || pSession->buffer2Host.empty())
        {
            // Do nothing
            return true;
        }
        else
        {
            // 如果服务器接口没问题，那到客户端的接口必然已经出错
            assert(pSession->toClientSockFail);

            if (!sendToHost(pSession))
            {
                spdlog::debug("[Session::onHostSockSend] send to host [{}] fail.", pSession->hostSoc.soc);
                pSession->toHostSockFail = true;
                return false;
            }
            else if (pSession->buffer2Host.empty())
            {
                spdlog::debug("[Session::onHostSockSend] last data to host[{}] has been sent.",
                              pSession->hostSoc.soc);
                pSession->toHostSockFail = true;
                return false;
            }
            else
            {
                return true;
            }
        }
    default:
        spdlog::error("[Session::onHostSockSend] invalid status: {}", pSession->status);
        return false;
    }

    return true;
}

bool Session::recvFromClient(Session_t *pSession)
{
    // receive data which from client
    int soc = pSession->clientSoc.soc;
    while (true)
    {
        char *buf;
        uint32_t bufSize;
        tie(buf, bufSize) = pSession->buffer2Host.getBuf();
        if (bufSize == 0)
        {
            pSession->fullFlag2Host = true;
            spdlog::trace("[Session::recvFromClient] to host buffer full");
            break;
        }

        int nRet = recv(soc, buf, bufSize, 0);
        if (nRet == 0)
        {
            spdlog::debug("[Session::recvFromClient] sock[{}] closed by client", soc);
            pSession->status == STATE_MACHINE::FAIL;
            pSession->toClientSockFail = true;
            break;
        }
        else if (nRet < 0)
        {
            if (errno == EAGAIN || errno == EWOULDBLOCK)
            {
                // recv action finished
                break;
            }

            spdlog::error("[Session::recvFromClient] recv sock[{}] fail: {}:[]",
                          soc, errno, strerror(errno));
            pSession->status == STATE_MACHINE::FAIL;
            pSession->toClientSockFail = true;
            break;
        }

        // adjust buffer settings
        pSession->buffer2Host.incEnd(nRet);
    }

    return sendToHost(pSession);
}

bool Session::recvFromHost(Session_t *pSession)
{
    // receive data which from host
    int soc = pSession->hostSoc.soc;
    while (true)
    {
        char *buf;
        uint32_t bufSize;
        tie(buf, bufSize) = pSession->buffer2Client.getBuf();
        if (bufSize == 0)
        {
            pSession->fullFlag2Client = true;
            spdlog::trace("[Session::recvFromHost] to client buffer full");
            break;
        }

        int nRet = recv(soc, buf, bufSize, 0);
        if (nRet == 0)
        {
            spdlog::debug("[Session::recvFromHost] sock[{}] closed by host", soc);
            pSession->status == STATE_MACHINE::FAIL;
            pSession->toHostSockFail = true;
            break;
        }
        else if (nRet < 0)
        {
            if (errno == EAGAIN || errno == EWOULDBLOCK)
            {
                // recv action finished
                break;
            }

            spdlog::error("[Session::recvFromHost] host sock[{}] recv fail: {}:[]",
                          soc, errno, strerror(errno));
            pSession->status == STATE_MACHINE::FAIL;
            pSession->toHostSockFail = true;
            break;
        }

        // adjust buffer settings
        pSession->buffer2Client.incEnd(nRet);
    }

    return sendToClient(pSession);
}

bool Session::sendToClient(Session_t *pSession)
{
    if (pSession->status == STATE_MACHINE::FAIL &&
        pSession->toClientSockFail)
    {
        // drop data due to client sock fail
        spdlog::error("[Session::sendToClient] drop data due to client sock fail");
        return false;
    }

    // forward data to client
    while (true)
    {
        char *buf;
        uint32_t bufSize;
        tie(buf, bufSize) = pSession->buffer2Client.getData();
        if (bufSize == 0)
        {
            // all data has been sent to client
            return true;
        }

        // send data to client
        int nRet = send(pSession->clientSoc.soc, buf, bufSize, 0);
        if (nRet < 0)
        {
            if (errno == EAGAIN || errno == EWOULDBLOCK)
            {
                // send action finished
                return true;
            }

            spdlog::error("[Session::sendToClient] send data to sock[{}] fail: {}:[]",
                          pSession->clientSoc.soc, errno, strerror(errno));
            pSession->status == STATE_MACHINE::FAIL;
            pSession->toClientSockFail = true;
            return false;
        }

        // adjust buffer settings
        pSession->buffer2Client.incStart(nRet);
    }
}

bool Session::sendToHost(Session_t *pSession)
{
    if (pSession->status == STATE_MACHINE::FAIL &&
        pSession->toHostSockFail)
    {
        // drop data due to host sock fail
        spdlog::error("[Session::sendToHost] drop data due to host sock fail");
        return false;
    }

    // forward data to host
    while (true)
    {
        char *buf;
        uint32_t bufSize;
        tie(buf, bufSize) = pSession->buffer2Host.getData();
        if (bufSize == 0)
        {
            // all data has been sent to host
            return true;
        }

        // send data to host
        int nRet = send(pSession->hostSoc.soc, buf, bufSize, 0);
        if (nRet < 0)
        {
            if (errno == EAGAIN || errno == EWOULDBLOCK)
            {
                // send action finished
                return true;
            }

            spdlog::error("[Session::sendToHost] send data to sock[{}] fail: {}:[]",
                          pSession->hostSoc.soc, errno, strerror(errno));
            pSession->status == STATE_MACHINE::FAIL;
            pSession->toHostSockFail = true;
            return false;
        }

        // adjust buffer settings
        pSession->buffer2Host.incStart(nRet);
    }
}

} // namespace mapper
