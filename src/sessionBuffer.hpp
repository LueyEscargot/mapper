/**
 * @file sessionBuffer.hpp
 * @author Liu Yu (source@liuyu.com)
 * @brief Session Buffer template class.
 *  Porting from project RosCar(https://github.com/liuyustudio/RosCar)
 * @version 1.0
 * @date 2019-10-07
 * 
 * @copyright Copyright (c) 2019
 * 
 */

#ifndef __MAPPER_COMMON_BUFFER_HPP__
#define __MAPPER_COMMON_BUFFER_HPP__

#include <string.h>
#include <tuple>

namespace mapper
{

template <int RECV_CAPACITY, int SEND_CAPACITY>
struct SessionBuffer
{
    using BufInfo = std::tuple<char *, unsigned int>;

    SessionBuffer() { init(); }

    inline void init()
    {
        recvStart = recvEnd = 0;
        sendStart = sendEnd = 0;
    }

    unsigned int recvStart, recvEnd;
    unsigned int sendStart, sendEnd;

    char recvBuf[RECV_CAPACITY];
    char sendBuf[SEND_CAPACITY];

    inline bool validate()
    {
        return recvStart >= 0 && recvStart <= recvEnd && recvEnd <= RECV_CAPACITY &&
               sendStart >= 0 && sendStart <= sendEnd && sendEnd <= SEND_CAPACITY;
    }

    inline bool recvBufEmpty() { return recvStart == recvEnd; }
    inline bool recvBufFull() { return RECV_CAPACITY == recvEnd ? !defragRecvBuf() : false; }
    inline bool sendBufEmpty() { return sendStart == sendEnd; }
    inline bool sendBufFull() { return SEND_CAPACITY == sendEnd ? !defragSendBuf() : false; }
    inline bool defragRecvBuf()
    {
        if (0 == recvStart)
        {
            return RECV_CAPACITY != recvEnd;
        }

        memmove(recvBuf, recvBuf + recvStart, recvStart);
        recvEnd -= recvStart;
        recvStart = 0;
        return true;
    }
    inline bool defragSendBuf()
    {
        if (0 == sendStart)
        {
            return SEND_CAPACITY != sendEnd;
        }

        memmove(sendBuf, sendBuf + sendStart, sendStart);
        sendEnd -= sendStart;
        sendStart = 0;
        return true;
    }
    inline bool defrag() { return defragRecvBuf() && defragSendBuf(); }

    inline unsigned int getRecvBufSize() { return recvBufFull() ? 0 : RECV_CAPACITY - recvEnd; }
    inline unsigned int getSendBufSize() { return sendBufFull() ? 0 : SEND_CAPACITY - sendEnd; }
    inline unsigned int getRecvDataSize() { return recvEnd - recvStart; }
    inline unsigned int getSendDataSize() { return sendEnd - sendStart; }

    inline BufInfo getRecvBuf()
    {
        unsigned int size = getRecvBufSize();
        return 0 < size ? BufInfo(recvBuf + recvEnd, size) : BufInfo(nullptr, 0);
    }
    inline BufInfo getSendBuf()
    {
        unsigned int size = getSendBufSize();
        return 0 < size ? BufInfo(sendBuf + sendEnd, size) : BufInfo(nullptr, 0);
    }

    inline BufInfo getRecvData()
    {
        unsigned int size = getRecvDataSize();
        return 0 < size ? BufInfo(recvBuf + recvStart, size) : BufInfo(nullptr, 0);
    }
    inline BufInfo getSendData()
    {
        unsigned int size = getSendDataSize();
        return 0 < size ? BufInfo(sendBuf + sendStart, size) : BufInfo(nullptr, 0);
    }

    inline void incRecvStart(uint32_t num)
    {
        recvStart += num;
        if (recvStart == recvEnd)
            recvStart = recvEnd = 0;
    }
    inline void incSendStart(uint32_t num)
    {
        sendStart += num;
        if (sendStart == sendEnd)
            sendStart = sendEnd = 0;
    }
    inline void incRecvEnd(uint32_t num) { recvEnd += num; }
    inline void incSendEnd(uint32_t num) { sendEnd += num; }
};

} // namespace mapper

#endif // __MAPPER_COMMON_BUFFER_HPP__
