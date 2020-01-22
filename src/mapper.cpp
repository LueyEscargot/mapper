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
}

bool Mapper::run(rapidjson::Document &cfg)
{
    spdlog::info("[Mapper::run]");

    // start net manager
    if (!mNetMgr.start(cfg))
    {
        spdlog::error("[Mapper::run] start net manager fail.");
        return false;
    }

    mNetMgr.join();

    spdlog::info("[Mapper::run] stop running.");

    return true;
}

void Mapper::stop()
{
    // stpo net manager
    spdlog::debug("[Mapper::stop] stop net manager.");
    mNetMgr.stop();
    spdlog::debug("[Mapper::stop] stop");
}

} // namespace mapper
