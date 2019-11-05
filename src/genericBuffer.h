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

#ifndef __MAPPER_GENERICBUFFER_H__
#define __MAPPER_GENERICBUFFER_H__

#include <stdint.h>
#include <string.h>
#include "buffer.h"

namespace mapper
{

class GenericBuffer : public Buffer
{
protected:
    GenericBuffer(uint32_t capacity);
    ~GenericBuffer();

public:
    static GenericBuffer *alloc(uint32_t capacity);
    static void release(GenericBuffer *pGenericBuffer);

    inline void init() override;
    inline char *getBuffer() override;
    inline char *getData() override;
    inline void incDataSize(uint64_t count) override;
    inline void incFreeSize(uint64_t count) override;
    inline uint64_t dataSize() override;
    inline uint64_t freeSize() override;
    inline bool empty() override;
    inline bool full() override;
    inline bool writable() override;
    inline bool defrag() override;
};

} // namespace mapper

#endif // __MAPPER_GENERICBUFFER_H__
