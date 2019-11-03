#ifndef __MAPPER_ENDPOINT_H__
#define __MAPPER_ENDPOINT_H__

namespace mapper
{

class Endpoint
{
public:
    typedef enum TYPE
    {
        SERVICE = 1,
        NORTH = 1 << 1,
        SOUTH = 1 << 2
    } Type_t;

    Endpoint(Type_t type, int soc, void *tag = nullptr);

    Type_t type;
    int soc;
    void *tag;
    bool valid;

    bool check() {
        return (type & (TYPE::SERVICE | TYPE::NORTH | TYPE::SOUTH));
    }
};

} // namespace mapper

#endif // __MAPPER_ENDPOINT_H__
