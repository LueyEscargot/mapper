/**
 * @file container.h
 * @author Liu Yu (source@liuyu.com)
 * @brief 一个靠外部更新时钟，并对对被加入对象按时间排序的简易超时容器
 * @version 2
 * @date 2019-11-09
 * @history
 *  2019-11-09  ver 1   创建基础版本
 *  2019-11-22  ver 2   用自建单向链表替代 std::list
 * 
 * @copyright Copyright (c) 2019
 * 
 */
#ifndef __MAPPER_TIMEER_CONTAINER_H__
#define __MAPPER_TIMEER_CONTAINER_H__

#include <assert.h>
#include <time.h>
#include <list>

namespace mapper
{
namespace timer
{

class Container
{
    static const uint32_t DEFAULT_TIMEOUT_INTERVAL = 30;

public:
    typedef enum TYPE
    {
        TIMER_CONNECT = 0,
        TIMER_ESTABLISHED,
        TIMER_BROKEN,
        TYPE_COUNT
    } Type_t;
    typedef struct CLIENT
    {
        time_t time;
        CLIENT *prev;
        CLIENT *next;
        Container *container;
        void *self;
    } Client_t;

    Container() : tail(&head) { head.next = nullptr; };
    ~Container();

    inline void setInterval(Type_t type, const uint32_t interval) { mTimeoutInterval[type] = interval; }
    inline uint32_t getInterval(Type_t type) { return mTimeoutInterval[type]; }

    inline bool empty() { return tail == &head; }
    void insert(Type_t type, time_t curTime, Client_t *pClient);
    inline void insert(time_t curTime, Client_t *pClient)
    {
        pClient->time = curTime;
        pClient->container = this;

        // insert at end of list and shift pointer 'tail' to new inserted item
        pClient->prev = tail;
        pClient->next = nullptr;
        tail->next = pClient;
        tail = pClient;
    }
    inline void remove(Client_t *pClient)
    {
        assert(pClient->container == this);

        pClient->prev->next = pClient->next;
        if (tail == pClient)
        {
            // remove last item
            assert(pClient->next == nullptr);
            tail = tail->prev;
        }
        else
        {
            pClient->next->prev = pClient->prev;
        }

        pClient->prev = pClient->next = nullptr;
        pClient->container = nullptr;
    }
    inline void refresh(time_t curTime, Client_t *pClient)
    {
        remove(pClient);
        insert(curTime, pClient);
    }

    Client_t *removeTimeout(time_t timePoint);

protected:
    Client_t head;
    Client_t *tail;

    Client_t mTimerPool[TYPE_COUNT];
    Client_t *mTimerTail[TYPE_COUNT];
    uint32_t mTimeoutInterval[TYPE_COUNT];
};

} // namespace timer
} // namespace mapper

#endif // __MAPPER_TIMEER_CONTAINER_H__
