#include "mapper.h"
#include <time.h>
#include <spdlog/spdlog.h>
#include "utils/jsonUtils.h"

using namespace std;
using namespace mapper::link;
using namespace mapper::utils;

namespace mapper
{

const uint32_t Mapper::STATISTIC_INTERVAL = 60;
const char *Mapper::STATISTIC_CONFIG_PATH = "/statistic/interval";

Mapper::Mapper()
    : mStop(false)
{
}

bool Mapper::run(rapidjson::Document &cfg)
{
    spdlog::info("[Mapper::run] start.");

    // create services
    spdlog::trace("[Mapper::run] create services");
    if (!Service::create(cfg, mServiceList))
    {
        spdlog::error("[Mapper::run] create services fail.");
        return false;
    }

    // statistic
    uint32_t statisticInterfal =
        JsonUtils::getAsUint32(cfg, STATISTIC_CONFIG_PATH, STATISTIC_INTERVAL);
    statisticInterfal = statisticInterfal < 1 ? 1 : statisticInterfal;
    time_t curTime = time(nullptr);

    spdlog::trace("[Mapper::run] start statistic routine");
    while (!mStop)
    {
        this_thread::sleep_for(chrono::seconds(statisticInterfal));

        curTime = time(nullptr);
        for (auto &service : mServiceList)
        {
            spdlog::info("{}: {}", service->name(), service->getStatistic(curTime));
            service->resetStatistic();
        };
    }
    spdlog::trace("[Mapper::run] stop statistic routine");

    spdlog::trace("[Mapper::run] join sub services");
    join();

    spdlog::info("[Mapper::run] stop.");

    return true;
}

void Mapper::join()
{
    // join net manager
    spdlog::trace("[Mapper::join] start join services.");
    for (auto &service : mServiceList)
    {
        service->join();
        spdlog::debug("[Mapper::join] close service[{}]", service->name());
        service->close();
    }
    spdlog::debug("[Mapper::join] stop all join");
}

void Mapper::stop()
{
    mStop = true;

    // close services
    spdlog::trace("[Mapper::stop] stop services.");
    for (auto &service : mServiceList)
    {
        service->stop();
    }
    spdlog::debug("[Mapper::stop] services closed");
}

} // namespace mapper
