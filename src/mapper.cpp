#include "mapper.h"
#include <assert.h>
#include <errno.h>
#include <string.h>
#include <spdlog/spdlog.h>

using namespace std;

namespace mapper
{

Mapper::Mapper(uint32_t bufSize)
    : mNetMgr(bufSize)
{
}

Mapper::~Mapper()
{
    release();
}

bool Mapper::run(const int maxSessions, vector<MapData_t> &mapDatas)
{
    spdlog::info("[Mapper::run] max sessions: {}", maxSessions);

    // start net manager
    if (!mNetMgr.start(maxSessions, mapDatas))
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
