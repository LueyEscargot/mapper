#include "mapper.h"
#include <assert.h>
#include <errno.h>
#include <string.h>
#include <spdlog/spdlog.h>

using namespace std;

namespace mapper
{

Mapper::Mapper()
    : mNetMgr()
{
}

Mapper::~Mapper()
{
    release();
}

bool Mapper::run(Config &cfg)
{
    spdlog::info("[Mapper::run]");

    // start net manager
    if (!mNetMgr.start(cfg))
    {
        spdlog::error("[Mapper::run] start net manager fail.");
        return false;
    }

    mNetMgr.join();

    return true;
}

void Mapper::release()
{
    // stpo net manager
    spdlog::debug("[Mapper::release] stpo net manager.");
    mNetMgr.stop();
}

} // namespace mapper
