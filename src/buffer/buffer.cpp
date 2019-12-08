#include "buffer.h"
#include <sstream>
#include <spdlog/spdlog.h>

#ifdef USE_RINGBUFFER
#include "ringBuffer.h"
#else
#include "genericBuffer.h"
#endif // USE_RINGBUFFER

using namespace std;

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
namespace buffer
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

bool Buffer::valid()
{
    return start <= end && end <= capacity;
}

std::string Buffer::toStr()
{
    stringstream ss;

    ss << "["
       << (buffer ? "_" : "x") << ","
       << capacity << ","
       << start << ","
       << end << ","
       << (stopRecv ? "x" : "_")
       << "]";

    return ss.str();
}

} // namespace buffer
} // namespace mapper
