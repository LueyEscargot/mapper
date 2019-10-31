#ifndef __MAPPER_ENDPOINT_H__
#define __MAPPER_ENDPOINT_H__

namespace mapper
{

class Endpoint
{
public:
    typedef enum TYPE
    {
        SERVICE = 0,
        NORTH,
        SOUTH
    } Type_t;

    Endpoint(Type_t type, int soc, void *tag = nullptr);

    Type_t type;
    int soc;
    void *tag;
};

} // namespace mapper

#endif // __MAPPER_ENDPOINT_H__
