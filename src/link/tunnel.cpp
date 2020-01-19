#include "tunnel.h"
#include <fcntl.h>
#include <arpa/inet.h>
#include <sys/epoll.h>
#include <spdlog/spdlog.h>
#include <sstream>
#include "endpoint.h"

using namespace std;

namespace mapper
{
namespace link
{

Tunnel_t *Tunnel::getTunnel()
{
    // TODO: use Tunnel buffer
    Tunnel_t *pt = new Tunnel_t;
    if (pt)
    {
        pt->init();
    }

    return pt;
}

void Tunnel::releaseTunnel(Tunnel_t *pt)
{
    // TODO: use Tunnel buffer
    if (pt)
    {
        delete pt;
    }
}

} // namespace link
} // namespace mapper
