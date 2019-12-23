/**
 * @file genericBuffer.h
 * @author Liu Yu (source@liuyu.com)
 * @brief Class for Generic Buffer.
 * @version 1.0
 * @date 2019-11-04
 * 
 * @copyright Copyright (c) 2019
 * 
 */

#ifndef __MAPPER_BUFFER_GENERICBUFFER_H__
#define __MAPPER_BUFFER_GENERICBUFFER_H__

#include <stdint.h>
#include <string.h>
#include "buffer.h"

namespace mapper
{
namespace buffer
{

class GenericBuffer : public Buffer
{
protected:
    GenericBuffer(uint32_t capacity);
    ~GenericBuffer();

public:
    static GenericBuffer *alloc(uint32_t capacity);
    static void release(GenericBuffer *pGenericBuffer);

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
};

} // namespace buffer
} // namespace mapper

#endif // __MAPPER_BUFFER_GENERICBUFFER_H__
