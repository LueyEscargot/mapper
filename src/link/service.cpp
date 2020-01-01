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

Service::~Service()
{
}

std::string Service::toStr()
{
    string str = R"([")" + mName + R"("])";
}

bool Service::init(int epollfd)
{
    mEpollfd = epollfd;

    return true;
}

} // namespace link
} // namespace mapper
