#include "service.h"
#include <spdlog/spdlog.h>

using namespace std;

namespace mapper
{
namespace link
{

Service::Service(std::string name)
    : mName(name)
{
}

bool Service::init(int epollfd)
{
    mEpollfd = epollfd;

    return true;
}

} // namespace link
} // namespace mapper
