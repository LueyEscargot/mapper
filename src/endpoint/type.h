#ifndef __MAPPER_ENDPOINT_TYPE_H__
#define __MAPPER_ENDPOINT_TYPE_H__

#include <string>

namespace mapper
{
namespace endpoint
{

    typedef enum TYPE
    {
        SERVICE = 1,
        NORTH = 1 << 1,
        SOUTH = 1 << 2
    } Type_t;

} // namespace endpoint
} // namespace mapper

#endif // __MAPPER_ENDPOINT_TYPE_H__
