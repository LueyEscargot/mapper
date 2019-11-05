#include "buffer.h"
#include <spdlog/spdlog.h>
#include "project.h"

#ifdef __linux__
#ifndef FORCE_USE_GENERIC_BUFFER
#define _USE_RINGBUFFER_ (1)
#endif // FORCE_USE_GENERIC_BUFFER
#endif // __linux__

#ifdef _USE_RINGBUFFER_
#include "ringBuffer.h"
#else
#include "genericBuffer.h"
#endif // _USE_RINGBUFFER_

class _Reporter
{
public:
    _Reporter()
    {
#ifdef _USE_RINGBUFFER_
        spdlog::info("buffer mode: RingBuffer.");
#else
        spdlog::info("buffer mode: GenericBuffer.");
#endif // _USE_RINGBUFFER_
    }
};

_Reporter _reporter;

namespace mapper
{

Buffer::Buffer(uint32_t _capacity)
    : buffer(nullptr),
      capacity(_capacity),
      start(0),
      end(0),
      stopRecv(false) {}
Buffer::~Buffer(){};

Buffer *Buffer::alloc(uint32_t capacity)
{
#ifdef _USE_RINGBUFFER_
    return RingBuffer::alloc(capacity);
#else
    return GenericBuffer::alloc(capacity);
#endif
}

void Buffer::release(Buffer *pBuffer)
{
#ifdef _USE_RINGBUFFER_
    return RingBuffer::release(static_cast<RingBuffer *>(pBuffer));
#else
    return GenericBuffer::release(static_cast<GenericBuffer *>(pBuffer));
#endif
}

} // namespace mapper
