/**
 * @file timeoutContainer.h
 * @author Liu Yu (source@liuyu.com)
 * @brief 一个靠外部更新时钟，并对对被加入对象按时间排序的简易超时容器
 * @version 1.0
 * @date 2019-11-09
 * 
 * @copyright Copyright (c) 2019
 * 
 */
#ifndef __MAPPER_TIMEOUTCONTAINER_H__
#define __MAPPER_TIMEOUTCONTAINER_H__

#include <assert.h>
#include <time.h>
#include <list>

namespace mapper
{

class TimeoutContainer
{
public:
    class Client
    {
        friend class TimeoutContainer;

    public:
        Client()
            : // inUse(false),
              time(0),
              mpContainer(nullptr),
              pos(nullptr)
        {
        }

        // inline bool busy() { return inUse; }
        inline TimeoutContainer *getContainer() { return mpContainer; }

    protected:
        // bool inUse;
        time_t time;
        TimeoutContainer *mpContainer;
        std::list<Client *>::const_iterator pos;
    };

    TimeoutContainer();
    ~TimeoutContainer();

    inline bool empty() { return mList.empty(); }
    inline void insert(time_t curTime, Client *pItem)
    {
        assert(pItem);

        pItem->time = curTime;
        pItem->mpContainer = this;
        pItem->pos = mList.insert(mList.end(), pItem);
    }
    inline void remove(Client *pItem)
    {
        mList.erase(pItem->pos);
        pItem->mpContainer = nullptr;
    }
    inline void refresh(time_t curTime, Client *pItem)
    {
        remove(pItem);
        insert(curTime, pItem);
    }

    std::list<Client *> removeTimeout(time_t timePoint);

protected:
    std::list<Client *> mList;
};

} // namespace mapper

#endif // __MAPPER_TIMEOUTCONTAINER_H__
