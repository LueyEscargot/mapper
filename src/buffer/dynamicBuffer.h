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

#include <netinet/in.h>
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
    struct BufBlk_t
    {
        BufBlk_t *__innerPrev;
        BufBlk_t *__innerNext;
        bool inUse;
        BufBlk_t *prev;
        BufBlk_t *next;
        sockaddr_in sockaddr;
        socklen_t sockaddr_len;
        uint32_t size;
        char buffer[0];
    };

    static const uint32_t BUFBLK_HEAD_SIZE = sizeof(BufBlk_t);

protected:
    DynamicBuffer();
    virtual ~DynamicBuffer();

public:
    static DynamicBuffer *allocDynamicBuffer(uint32_t capacity);
    static void releaseDynamicBuffer(DynamicBuffer *pDynamicBuffer);

    void *reserve(int size);
    BufBlk_t *cut(uint32_t size);
    void release(BufBlk_t *pBuffer);

protected:
    static void init(DynamicBuffer::BufBlk_t *pBlk,
                     uint32_t size,
                     BufBlk_t *innerlink_prev,
                     BufBlk_t *innerlink_next);

    void *mBuffer;
    BufBlk_t *mpFreePos;
};

} // namespace buffer
} // namespace mapper

#endif // __MAPPER_BUFFER_DYNAMICBUFFER_H__
