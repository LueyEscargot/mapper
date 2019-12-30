#include "service.h"
#include <spdlog/spdlog.h>

using namespace std;
using namespace mapper::config;

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

bool Service::init(config::Config *pConf, int epollfd)
{
    mpConf = pConf;
    mEpollfd = epollfd;
    return true;
}

std::string Service::toStr()
{
    string str = R"([")" + mName + R"("])";
}

} // namespace link
} // namespace mapper
