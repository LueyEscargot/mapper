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
#include <assert.h>
#include <time.h>
#include <list>

namespace mapper
{
namespace utils
{

class TimerList : public BaseList
{
public:
    struct Entity_t : public BaseList::Entity_t
    {
        time_t time;
        TimerList *timer;

        inline void init(void *container)
        {
            BaseList::Entity_t::init(container);
            time = 0;
            timer = nullptr;
        }
    };

    TimerList() : mLastRefreshTime(0) {}
    virtual ~TimerList(){};

    inline void push_front(time_t curTime, Entity_t *p)
    {
        p->time = curTime;
        BaseList::push_front(p);
    }
    inline void push_back(time_t curTime, Entity_t *p)
    {
        assert(!p->inList);

        p->time = curTime;
        p->timer = this;
        BaseList::push_back(p);
    }
    inline void erase(Entity_t *p)
    {
        assert(p->timer == this);
        p->timer = nullptr;
        BaseList::erase(p);
    }
    void refresh(time_t curTime, Entity_t *p);
    void removeTimeout(time_t timeoutTime, std::list<Entity_t *> &timeoutList);
    void getTimeoutList(time_t timeoutTime, std::list<Entity_t *> &timeoutList);

protected:
    time_t mLastRefreshTime;
};

} // namespace utils
} // namespace mapper

#endif // __MAPPER_UTILS_TIMERLIST_H__
