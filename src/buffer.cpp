#include "buffer.h"

#ifdef __linux__
#define _USE_RINGBUFFER_ (1)
#endif

#ifdef _USE_RINGBUFFER_
#include "ringBuffer.h"
#else
#include "genericBuffer.h"
#endif

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
