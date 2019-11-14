#include "buffer.h"
#include <spdlog/spdlog.h>

#ifdef USE_RINGBUFFER
#include "ringBuffer.h"
#else
#include "genericBuffer.h"
#endif // USE_RINGBUFFER

class _Reporter
{
public:
    _Reporter()
    {
#ifdef USE_RINGBUFFER
        spdlog::info("buffer mode: RingBuffer.");
#else
        spdlog::info("buffer mode: GenericBuffer.");
#endif // USE_RINGBUFFER
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
#ifdef USE_RINGBUFFER
    return RingBuffer::alloc(capacity);
#else
    return GenericBuffer::alloc(capacity);
#endif
}

void Buffer::release(Buffer *pBuffer)
{
#ifdef USE_RINGBUFFER
    return RingBuffer::release(static_cast<RingBuffer *>(pBuffer));
#else
    return GenericBuffer::release(static_cast<GenericBuffer *>(pBuffer));
#endif
}

} // namespace mapper