/**
 * @file ringBuffer.h
 * @author Liu Yu (source@liuyu.com)
 * @brief Class for Ring Buffer.
 * @version 1.0
 * @date 2019-11-01
 * 
 * @copyright Copyright (c) 2019
 * 
 */

#ifndef __MAPPER_RINGBUFFER_H__
#define __MAPPER_RINGBUFFER_H__

#include <stdint.h>
#include "buffer.h"

namespace mapper
{

class RingBuffer : public Buffer
{
protected:
    RingBuffer(uint32_t capacity);
    ~RingBuffer();

public:
    static RingBuffer *alloc(uint32_t capacity);
    static void release(RingBuffer *pRingBuffer);

    void init() override;
    char *getBuffer() override;
    char *getData() override;
    void incDataSize(uint64_t count) override;
    void incFreeSize(uint64_t count) override;
    uint64_t dataSize() override;
    uint64_t freeSize() override;
    bool empty() override;
    bool full() override;
    bool writable() override;
    bool defrag() override;

protected:
    static uint64_t alignToPageSize(uint64_t size);
};

} // namespace mapper

#endif // __MAPPER_RINGBUFFER_H__
