#include "mapper.h"
#include <assert.h>
#include <errno.h>
#include <string.h>
#include <spdlog/spdlog.h>

using namespace std;

namespace mapper
{

Mapper::Mapper() {}

Mapper::~Mapper()
{
    release();
}

bool Mapper::run(const int maxSessions)
{
    spdlog::info("[Mapper::run] max sessions: {}", maxSessions);

    if (!init(maxSessions))
    {
        spdlog::error("[Mapper::run] init fail.");
        return false;
    }

    return true;
}

bool Mapper::init(const int maxSessions)
{
    // init session manager
    if (!mSessionMgr.init(maxSessions))
    {
        spdlog::error("[Mapper::init] session manager init fail.");
        return false;
    }

    return true;
}

void Mapper::release()
{
    // release session
    spdlog::debug("[Mapper::release] release session.");
    mSessionMgr.release();
}

} // namespace mapper
