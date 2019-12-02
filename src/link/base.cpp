#include "base.h"
#include <sstream>

using namespace std;

namespace mapper
{
namespace link
{

Base::Base(Type_t _type, int _soc, void *_tag)
    : type(_type),
      soc(_soc),
      tag(_tag),
      valid(true)
{
}

string Base::toStr()
{
    stringstream ss;

    ss << "["
       << ((type == Type_t::SERVICE)
               ? "Service"
               : (type == Type_t::NORTH)
                     ? "North"
                     : "South")
       << ","
       << soc
       << ","
       << tag
       << "]";

    return ss.str();
}

} // namespace link
} // namespace mapper
