/**
 * @file genericBuffer.h
 * @author Liu Yu (source@liuyu.com)
 * @brief Class for Base Class of Buffer.
 * @version 1.0
 * @date 2019-11-04
 * 
 * @copyright Copyright (c) 2019
 * 
 */

#ifndef __MAPPER_BUFFER_BUFFER_H__
#define __MAPPER_BUFFER_BUFFER_H__

#include <stdint.h>
#include <string>

namespace mapper
{
namespace buffer
{

class Buffer
{
protected:
    Buffer(uint32_t _capacity);
    virtual ~Buffer();

public:
    static Buffer *alloc(uint32_t capacity);
    static void release(Buffer *pBuffer);

    virtual void init() = 0;
    virtual char *getBuffer() = 0;
    virtual char *getData() = 0;
    virtual void incDataSize(uint64_t count) = 0;
    virtual void incFreeSize(uint64_t count) = 0;
    virtual uint64_t dataSize() = 0;
    virtual uint64_t freeSize() = 0;
    virtual bool empty() = 0;
    virtual bool full() = 0;
    virtual bool writable() = 0;
    virtual bool defrag() = 0;

    virtual bool valid();
    virtual std::string toStr();

    bool stopRecv;

protected:
    char *buffer;
    uint64_t capacity;
    uint64_t start;
    uint64_t end;
};

} // namespace buffer
} // namespace mapper

#endif // __MAPPER_BUFFER_BUFFER_H__
