/**
 * @file sessionBuffer.hpp
 * @author Liu Yu (source@liuyu.com)
 * @brief Session Buffer template class.
 *  Porting from project RosCar(https://github.com/liuyustudio/RosCar)
 * @version 1.0
 * @date 2019-10-07
 * 
 * @copyright Copyright (c) 2019
 * 
 */

#ifndef __MAPPER_COMMON_BUFFER_HPP__
#define __MAPPER_COMMON_BUFFER_HPP__

#include <string.h>
#include <tuple>

namespace mapper
{

template <int TO_NORTH_CAPACITY, int TO_SOUTH_CAPACITY>
struct SessionBuffer
{
    using BufInfo = std::tuple<char *, unsigned int>;

    SessionBuffer() { init(); }

    inline void init()
    {
        toNorthStart = toNorthEnd = 0;
        toSouthStart = toSouthEnd = 0;
    }

    unsigned int toNorthStart, toNorthEnd;
    unsigned int toSouthStart, toSouthEnd;

    char toNorthBuf[TO_NORTH_CAPACITY];
    char toSouthBuf[TO_SOUTH_CAPACITY];

    inline bool validate()
    {
        return toNorthStart >= 0 && toNorthStart <= toNorthEnd && toNorthEnd <= TO_NORTH_CAPACITY &&
               toSouthStart >= 0 && toSouthStart <= toSouthEnd && toSouthEnd <= TO_SOUTH_CAPACITY;
    }

    inline bool toNorthBufEmpty() { return toNorthStart == toNorthEnd; }
    inline bool toNorthBufFull() { return TO_NORTH_CAPACITY == toNorthEnd ? !defragToNorthBuf() : false; }
    inline bool toSouthBufEmpty() { return toSouthStart == toSouthEnd; }
    inline bool toSouthBufFull() { return TO_SOUTH_CAPACITY == toSouthEnd ? !defragToSouthBuf() : false; }
    inline bool defragToNorthBuf()
    {
        if (0 == toNorthStart)
        {
            return TO_NORTH_CAPACITY != toNorthEnd;
        }

        memmove(toNorthBuf, toNorthBuf + toNorthStart, toNorthStart);
        toNorthEnd -= toNorthStart;
        toNorthStart = 0;
        return true;
    }
    inline bool defragToSouthBuf()
    {
        if (0 == toSouthStart)
        {
            return TO_SOUTH_CAPACITY != toSouthEnd;
        }

        memmove(toSouthBuf, toSouthBuf + toSouthStart, toSouthStart);
        toSouthEnd -= toSouthStart;
        toSouthStart = 0;
        return true;
    }
    inline bool defrag() { return defragToNorthBuf() && defragToSouthBuf(); }

    inline unsigned int getToNorthBufSize() { return toNorthBufFull() ? 0 : TO_NORTH_CAPACITY - toNorthEnd; }
    inline unsigned int getToSouthBufSize() { return toSouthBufFull() ? 0 : TO_SOUTH_CAPACITY - toSouthEnd; }
    inline unsigned int getToNorthDataSize() { return toNorthEnd - toNorthStart; }
    inline unsigned int getToSouthDataSize() { return toSouthEnd - toSouthStart; }

    inline BufInfo getToNorthBuf()
    {
        unsigned int size = getToNorthBufSize();
        return 0 < size ? BufInfo(toNorthBuf + toNorthEnd, size) : BufInfo(nullptr, 0);
    }
    inline BufInfo getToSouthBuf()
    {
        unsigned int size = getToSouthBufSize();
        return 0 < size ? BufInfo(toSouthBuf + toSouthEnd, size) : BufInfo(nullptr, 0);
    }

    inline BufInfo getToNorthData()
    {
        unsigned int size = getToNorthDataSize();
        return 0 < size ? BufInfo(toNorthBuf + toNorthStart, size) : BufInfo(nullptr, 0);
    }
    inline BufInfo getToSouthData()
    {
        unsigned int size = getToSouthDataSize();
        return 0 < size ? BufInfo(toSouthBuf + toSouthStart, size) : BufInfo(nullptr, 0);
    }

    inline void incToNorthStart(uint32_t num)
    {
        toNorthStart += num;
        if (toNorthStart == toNorthEnd)
            toNorthStart = toNorthEnd = 0;
    }
    inline void incToSouthStart(uint32_t num)
    {
        toSouthStart += num;
        if (toSouthStart == toSouthEnd)
            toSouthStart = toSouthEnd = 0;
    }
    inline void incToNorthEnd(uint32_t num) { toNorthEnd += num; }
    inline void incToSouthEnd(uint32_t num) { toSouthEnd += num; }
};

} // namespace mapper

#endif // __MAPPER_COMMON_BUFFER_HPP__
