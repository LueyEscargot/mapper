/**
 * @file main.cpp
 * @author Liu Yu (source@liuyu.com)
 * @brief main
 * @version 0.1
 * @date 2019-10-07
 * 
 * @copyright Copyright (c) 2019
 * 
 */
#include <stdlib.h>
#include <stdio.h>
#include <vector>
#include <spdlog/spdlog.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include "mapper.h"
#include "define.h"

static const char *CONFIG_FILE = "config.json";
static const char *LOG_FILE = "mapper.log";
static const auto LOG_LEVEL = spdlog::level::debug;

void init();
mapper::Mapper gMapper;

int main(int argc, char *argv[])
{
    init();

    spdlog::info("Start");

    std::vector<mapper::MapData_t> mapDatas;
    mapper::MapData_t mapData1(6911, "192.168.1.66", 6911);
    mapper::MapData_t mapData2(8118, "192.168.1.66", 8118);

    mapDatas.push_back(mapData1);
    mapDatas.push_back(mapData2);

    // run mapper
    if (!gMapper.run(mapper::MAX_SESSIONS, &mapDatas))
    {
        spdlog::error("[main] run mapper fail");
        std::exit(EXIT_FAILURE);
    }

    spdlog::info("Stop");

    return EXIT_SUCCESS;
}

void init()
{
    // init logger
    auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
    console_sink->set_level(LOG_LEVEL);
    auto file_sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(LOG_FILE, true);
    file_sink->set_level(LOG_LEVEL);
    spdlog::set_default_logger(
        std::make_shared<spdlog::logger>(
            "mapper", spdlog::sinks_init_list({console_sink, file_sink})));
}
