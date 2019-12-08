#include "ringBuffer.h"
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

struct Constant
{
    const uint64_t DEFAULT_PAGESIZE = 1 << 12L;
    uint32_t pageSize = DEFAULT_PAGESIZE;
    Constant()
    {
        pageSize = sysconf(_SC_PAGE_SIZE);
        pageSize = pageSize <= 0 ? DEFAULT_PAGESIZE : pageSize;
    }
};
Constant Const;

RingBuffer::RingBuffer(uint32_t capacity)
    : Buffer(capacity)
{
}

RingBuffer::~RingBuffer()
{
    if (buffer)
    {
        if (munmap(buffer + capacity, capacity))
        {
            spdlog::error("[RingBuffer::releaseBuffer] release second memory mapping fail. {}: {}.",
                          errno, strerror(errno));
        }
        if (munmap(buffer, capacity))
        {
            spdlog::error("[RingBuffer::releaseBuffer] release first memory mapping fail. {}: {}.",
                          errno, strerror(errno));
        }
        if (munmap(buffer, capacity << 1))
        {
            spdlog::error("[RingBuffer::releaseBuffer] release main memory mapping fail. {}: {}.",
                          errno, strerror(errno));
        }
        buffer = nullptr;
    }
}

RingBuffer *RingBuffer::alloc(uint32_t capacity)
{
    RingBuffer *pRingBuffer = new RingBuffer(capacity);
    if (!pRingBuffer)
    {
        spdlog::error("[RingBuffer::createBuffer] create RainBuffer object fail.");
        return nullptr;
    }
    if ([&]() -> bool {
            // align capacity to system page size;
            capacity = alignToPageSize(capacity);
            spdlog::trace("[RingBuffer::createBuffer] capacity: {}", capacity);

            // create fd for memory block
            char path[] = "/dev/shm/MAPPER-RINGBUFFER-XXXXXX";
            int fd = mkstemp(path);
            if (fd < 0)
            {
                spdlog::error("[RingBuffer::createBuffer] open unique temporary file fail. {}: {}.",
                              errno, strerror(errno));
                return false;
            }
            if ([&]() -> bool {
                    if (unlink(path))
                    {
                        spdlog::error("[RingBuffer::createBuffer] unlink new created unique temporary file fail. {}: {}.",
                                      errno, strerror(errno));
                        return false;
                    }
                    if (ftruncate(fd, capacity))
                    {
                        spdlog::error("[RingBuffer::createBuffer] allocate memory[{}] fail. {}: {}.",
                                      capacity, errno, strerror(errno));
                        return false;
                    }

                    // map memory block to virtual memory space
                    pRingBuffer->buffer = static_cast<char *>(mmap(NULL,
                                                                    capacity << 1,
                                                                    PROT_NONE, MAP_ANONYMOUS | MAP_PRIVATE,
                                                                    -1,
                                                                    0));
                    if (pRingBuffer->buffer == MAP_FAILED)
                    {
                        spdlog::error("[RingBuffer::createBuffer] query virtual space fail. {}: {}.",
                                      errno, strerror(errno));
                        return false;
                    }
                    if ([&]() -> bool {
                            if (mmap(pRingBuffer->buffer, capacity,
                                     PROT_READ | PROT_WRITE, MAP_FIXED | MAP_SHARED | MAP_LOCKED,
                                     fd,
                                     0) !=
                                pRingBuffer->buffer)
                            {
                                spdlog::error("[RingBuffer::createBuffer] map fd to first virtual space fail. {}: {}.",
                                              errno, strerror(errno));
                                return false;
                            }
                            if (mmap(pRingBuffer->buffer + capacity, capacity,
                                     PROT_READ | PROT_WRITE, MAP_FIXED | MAP_SHARED | MAP_LOCKED, fd, 0) !=
                                pRingBuffer->buffer + capacity)
                            {
                                spdlog::error("[RingBuffer::createBuffer] map fd to second virtual space fail. {}: {}.",
                                              errno, strerror(errno));
                                return false;
                            }
                            return true;
                        }())
                    {
                        return true;
                    }
                    else
                    {
                        munmap(pRingBuffer->buffer, capacity << 1);
                        return false;
                    }
                }())
            {
                close(fd);
                return true;
            }
            else
            {
                close(fd);
                return false;
            }
        }())
    {
        return pRingBuffer;
    }
    else
    {
        delete pRingBuffer;
        return nullptr;
    }
}

void RingBuffer::release(RingBuffer *pRingBuffer)
{
    delete pRingBuffer;
}

void RingBuffer::init() { start = end = 0, stopRecv = false; }
char *RingBuffer::getBuffer() { return buffer + end; }
char *RingBuffer::getData() { return buffer + start; }
void RingBuffer::incDataSize(uint64_t count) { end += count; }
void RingBuffer::incFreeSize(uint64_t count)
{
    start += count;

    if (start >= capacity)
    {
        start -= capacity;
        end -= capacity;
    }
}
uint64_t RingBuffer::dataSize() { return end - start; }
uint64_t RingBuffer::freeSize() { return capacity - dataSize(); }
bool RingBuffer::empty() { return !dataSize(); }
bool RingBuffer::full() { return !freeSize(); }
bool RingBuffer::writable() { return dataSize(); }
bool RingBuffer::defrag() { return true; }

uint64_t RingBuffer::alignToPageSize(uint64_t size)
{
    uint64_t m = size % Const.pageSize;
    return m ? size + Const.pageSize - m : size;
}

} // namespace buffer
} // namespace mapper
