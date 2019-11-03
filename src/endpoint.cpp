#include "endpoint.h"

using namespace std;

namespace mapper
{

Endpoint::Endpoint(Type_t _type, int _soc, void *_tag)
    : type(_type),
      soc(_soc),
      tag(_tag),
      valid(true)
{
}

} // namespace mapper
