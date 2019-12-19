/**
 * @file container.h
 * @author Liu Yu (source@liuyu.com)
 * @brief 一个靠外部更新时钟，并对对被加入对象按时间排序的简易超时容器
 * @version 2
 * @date 2019-11-09
 * @history
 *  2019-11-09  ver 1   创建基础版本
 *  2019-11-22  ver 2   用自建单向链表替代 std::list
 *  2019-12-08  ver 3   新增定时器类型定义
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
    static const int DEFAULT_TIMEOUT_INTERVAL = 30;

public:
    typedef enum TYPE
    {
        TYPE_INVALID = 0,
        TIMER_CONNECT,
        TIMER_ESTABLISHED,
        TIMER_BROKEN,
        TYPE_COUNT
    } Type_t;
    typedef struct CLIENT
    {
        bool inTimer;
        time_t time;
        CLIENT *prev;
        CLIENT *next;
        Type_t type;
        void *tag; // 客户端自维护指针，此类不负责初始化、生命周期管理、校验、释放等等

        inline void init()
        {
            inTimer = false;
            time = 0;
            prev = nullptr;
            next = nullptr;
            type = Type_t::TYPE_INVALID;
        }
    } Client_t;

    Container();
    ~Container();

    inline void setInterval(Type_t type, const int interval) { mTimeoutInterval[type] = interval; }
    inline int getInterval(Type_t type) { return mTimeoutInterval[type]; }

    inline bool empty(Type_t type) { return mHead[type] == nullptr; }
    void insert(Type_t type, time_t curTime, Client_t *pClient);
    void remove(Client_t *pClient);
    inline void refresh(time_t curTime, Client_t *pClient)
    {
        if (pClient)
        {
            remove(pClient);
            insert(pClient->type, curTime, pClient);
        }
    }

    Client_t *removeTimeout(Type_t type, time_t curTime);

    bool isInTimer(Type_t type, Client_t *pClient);

protected:
    inline bool checkStatChange(Client_t *pClient, Type_t newType)
    {
        return StateMachine[pClient->type][newType];
    }

    static bool StateMachine[TYPE_COUNT][TYPE_COUNT];

    Client_t *mHead[TYPE_COUNT];
    Client_t *mTail[TYPE_COUNT];
    int mTimeoutInterval[TYPE_COUNT];
};

} // namespace timer
} // namespace mapper

#endif // __MAPPER_TIMEER_CONTAINER_H__
