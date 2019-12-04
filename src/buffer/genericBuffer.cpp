#include "genericBuffer.h"
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <sys/mman.h>
#include <spdlog/spdlog.h>

using namespace std;

namespace mapper
{
namespace buffer
{

GenericBuffer::GenericBuffer(uint32_t _capacity)
    : Buffer(_capacity) {}

GenericBuffer::~GenericBuffer()
{
    if (buffer)
    {
        free(buffer);
        buffer = nullptr;
    }
}

GenericBuffer *GenericBuffer::alloc(uint32_t capacity)
{
    assert(capacity);

    GenericBuffer *pGenericBuffer = new GenericBuffer(capacity);
    if (!pGenericBuffer)
    {
        spdlog::error("[GenericBuffer::createBuffer] create RainBuffer object fail.");
        return nullptr;
    }

    spdlog::trace("[GenericBuffer::createBuffer] capacity: {}", capacity);
    pGenericBuffer->buffer = static_cast<char *>(malloc(capacity));
    if (pGenericBuffer->buffer == nullptr)
    {
        spdlog::error("[GenericBuffer::createBuffer] malloc {} bytes fail.", capacity);
        delete pGenericBuffer;
        return nullptr;
    }
    else
    {
        return pGenericBuffer;
    }
}

void GenericBuffer::release(GenericBuffer *pGenericBuffer)
{
    delete pGenericBuffer;
}

void GenericBuffer::init() { start = end = 0, stopRecv = false; }
char *GenericBuffer::getBuffer() { return buffer + end; }
char *GenericBuffer::getData() { return buffer + start; }
void GenericBuffer::incDataSize(uint64_t count)
{
    end += count;
    // assert(valid());
}
void GenericBuffer::incFreeSize(uint64_t count)
{
    start += count;
    if (start == end)
        start = end = 0;
    // assert(valid());
}
uint64_t GenericBuffer::dataSize() { return end - start; }
uint64_t GenericBuffer::freeSize() { return capacity - end; }
bool GenericBuffer::empty() { return !dataSize(); }
bool GenericBuffer::full() { return !freeSize() ? !defrag() : false; }
bool GenericBuffer::writable() { return dataSize(); }
bool GenericBuffer::defrag()
{
    if (0 == start)
    {
        return capacity != end;
    }

    memmove(buffer, buffer + start, start);
    end -= start;
    start = 0;
    return true;
}

} // namespace buffer
} // namespace mapper
