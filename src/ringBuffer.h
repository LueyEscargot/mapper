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

namespace mapper
{

class RingBuffer
{
protected:
    RingBuffer(uint32_t capacity);
    ~RingBuffer();

public:
    static RingBuffer *alloc(uint32_t capacity);
    static void release(RingBuffer *pRingBuffer);

    inline void init() { readPos = writePos = 0, stopRecv = false; }
    inline char *getWritePos() { return address + writePos; }
    inline char *getReadPos() { return address + readPos; }
    inline void incWritePos(uint64_t count) { writePos += count; }
    inline void incReadPos(uint64_t count)
    {
        readPos += count;

        if (readPos >= capacity)
        {
            readPos -= capacity;
            writePos -= capacity;
        }
    }
    inline uint64_t dataSize() { return writePos - readPos; }
    inline uint64_t freeSize() { return capacity - dataSize(); }
    inline bool empty() { return !dataSize(); }
    inline bool full() { return !freeSize(); }
    inline bool writable() { return dataSize(); }

    bool stopRecv;

protected:
    static uint64_t alignToPageSize(uint64_t size);

    char *address;
    uint64_t capacity;
    uint64_t readPos;
    uint64_t writePos;
};

} // namespace mapper

#endif // __MAPPER_RINGBUFFER_H__
