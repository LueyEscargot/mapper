/**
 * @file timerList.h
 * @author Liu Yu (source@liuyu.com)
 * @brief class of timer list
 * @version 1.0
 * @date 2020-01-13
 * 
 * @copyright Copyright (c) 2020
 * 
 */
#ifndef __MAPPER_UTILS_TIMERLIST_H__
#define __MAPPER_UTILS_TIMERLIST_H__

#include "baseList.h"
#include <time.h>

namespace mapper
{
namespace utils
{

class TimerList : public BaseList
{
public:
    struct Entry_t : public BaseList::Entry_t
    {
        time_t time;

        inline void init(void *container)
        {
            BaseList::Entry_t::init(container);
            time = 0;
        }
    };

    TimerList() : mLastRefreshTime(0) {}
    virtual ~TimerList(){};

    inline void push_front(time_t curTime, Entry_t *p)
    {
        p->time = curTime;
        BaseList::push_front(p);
    }
    inline void push_back(time_t curTime, Entry_t *p)
    {
        p->time = curTime;
        BaseList::push_back(p);
    }
    inline void erase(Entry_t *p) { BaseList::erase(p); }
    void refresh(time_t curTime, Entry_t *p);

protected:
    time_t mLastRefreshTime;
};

} // namespace utils
} // namespace mapper

#endif // __MAPPER_UTILS_TIMERLIST_H__
