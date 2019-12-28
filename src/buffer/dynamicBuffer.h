/**
 * @file dynamicBuffer.h
 * @author Liu Yu (source@liuyu.com)
 * @brief Class for Heap Buffer.
 * @version 1.0
 * @date 2019-12-24
 * 
 * @copyright Copyright (c) 2019
 * 
 */

#ifndef __MAPPER_BUFFER_DYNAMICBUFFER_H__
#define __MAPPER_BUFFER_DYNAMICBUFFER_H__

#include <sys/socket.h>
#include <list>
#include <tuple>
#include <string>

namespace mapper
{
namespace buffer
{

class DynamicBuffer
{
public:
    typedef struct BUFBLK
    {
        bool inUse;
        BUFBLK *prev;
        BUFBLK *next;
        sockaddr_storage peer_addr;
        socklen_t peer_addr_len;
        uint32_t size;
        char buffer[0];

        void init(uint32_t _size, BUFBLK *_prev, BUFBLK *_next)
        {
            inUse = false;
            prev = _prev;
            next = _next;
            size = _size;
        }
    } BufBlk_t;

    static const uint32_t BUFBLK_HEAD_SIZE = sizeof(BufBlk_t);

protected:
    DynamicBuffer();
    virtual ~DynamicBuffer();

public:
    static DynamicBuffer *alloc(uint32_t capacity);
    static void release(DynamicBuffer *pDynamicBuffer);

    void *reserve(int size);
    BufBlk_t *cut(uint32_t size);
    void putBack(BufBlk_t *pBuffer);

protected:
    void *mBuffer;
    BufBlk_t *mpFreePos;
};

} // namespace buffer
} // namespace mapper

#endif // __MAPPER_BUFFER_DYNAMICBUFFER_H__
