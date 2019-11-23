#ifndef __MAPPER_ENDPOINT_BASE_H__
#define __MAPPER_ENDPOINT_BASE_H__

#include <string>
#include "type.h"

namespace mapper
{
namespace endpoint
{

class Base
{
public:
    Base(Type_t type) : Base(type, 0) {}
    Base(Type_t type, int soc) : Base(type, soc, nullptr) {}
    Base(Type_t type, int soc, void *tag);

    Type_t type;
    int soc;
    void *tag;
    bool valid;

    virtual std::string toStr();
};

} // namespace endpoint
} // namespace mapper

#endif // __MAPPER_ENDPOINT_BASE_H__
