/**
 * @file baseList.h
 * @author Liu Yu (source@liuyu.com)
 * @brief base class of list
 * @version 1.0
 * @date 2020-01-13
 * 
 * @copyright Copyright (c) 2020
 * 
 */
#ifndef __MAPPER_UTILS_LIST_H__
#define __MAPPER_UTILS_LIST_H__

namespace mapper
{
namespace utils
{

class BaseList
{
public:
    struct Entry_t
    {
        bool inList;
        Entry_t *prev;
        Entry_t *next;
        void *container;

        inline void init(void *_container)
        {
            inList = false;
            prev = next = nullptr;
            container = _container;
        }
    };

    BaseList();
    virtual ~BaseList();

    void push_front(Entry_t *p);
    void push_back(Entry_t *p);
    void erase(Entry_t *p);
    bool check();

    Entry_t *mpHead;
    Entry_t *mpTail;
};

} // namespace utils
} // namespace mapper

#endif // __MAPPER_UTILS_LIST_H__
