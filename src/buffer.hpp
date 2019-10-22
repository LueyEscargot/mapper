/**
 * @file buffer.hpp
 * @author Liu Yu (source@liuyu.com)
 * @brief Buffer template class.
 *  Porting from project RosCar(https://github.com/liuyustudio/RosCar)
 * @version 1.0
 * @date 2019-10-07
 * 
 * @copyright Copyright (c) 2019
 * 
 */

#ifndef __MAPPER_BUFFER_HPP__
#define __MAPPER_BUFFER_HPP__

#include <assert.h>
#include <string.h>
#include <tuple>

namespace mapper
{

template <uint32_t CAPACITY>
struct Buffer
{
    using BufInfo = std::tuple<char *, uint32_t>;

    char buffer[CAPACITY];
    uint32_t start, end;

    Buffer() { assert(CAPACITY > 0), init(); }
    inline void init() { start = end = 0; }
    inline bool validate() { return start >= 0 && start <= end && end <= CAPACITY; }
    inline bool empty() { return start == end; }
    inline bool full() { return CAPACITY == end ? !defrag() : false; }
    inline bool defrag()
    {
        if (0 == start)
        {
            return CAPACITY != end;
        }

        memmove(buffer, buffer + start, start);
        end -= start;
        start = 0;
        return true;
    }

    inline uint32_t getBufSize() { return full() ? 0 : CAPACITY - end; }
    inline uint32_t getDataSize() { return end - start; }

    inline BufInfo getBuf()
    {
        uint32_t size = getBufSize();
        return size ? BufInfo(buffer + end, size) : BufInfo(nullptr, 0);
    }

    inline BufInfo getData()
    {
        uint32_t size = getDataSize();
        return size ? BufInfo(buffer + start, size) : BufInfo(nullptr, 0);
    }

    inline void incStart(uint32_t num)
    {
        start += num;
        if (start == end)
            start = end = 0;
        assert(start <= end);
    }
    inline void incEnd(uint32_t num) { end += num, assert(end <= CAPACITY); }
};

} // namespace mapper

#endif // __MAPPER_BUFFER_HPP__
