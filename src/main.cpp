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
#include <algorithm>
#include <exception>
#include <vector>
#include <spdlog/spdlog.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include "config.h"
#include "define.h"
#include "mapper.h"
#include "project.h"

static const char *CONFIG_FILE = "config.ini";
static const char *LOG_FILE = "mapper.log";
static const auto LOG_LEVEL = spdlog::level::debug;

void showSantax(char *argv[]);
void init(int argc, char *argv[]);
bool hasArg(char **begin, char **end, const std::string &arg);
char *getArg(char **begin, char **end, const std::string &arg);
void getArgMapData(int argc, char *argv[], const char *arg, std::vector<std::string> &argMapData);

int32_t gSessions = 0;
mapper::Config gConf;
mapper::Mapper gMapper(mapper::BUFFER_SIZE);

int main(int argc, char *argv[])
{
    if (hasArg(argv, argv + argc, "-h"))
    {
        showSantax(argv);
        return 0;
    }

    init(argc, argv);

    spdlog::info("{} Start", projectDesc());

    // run mapper
    spdlog::debug("[main] run mapper");
    if (!gMapper.run(gSessions, gConf.getMapData()))
    {
        spdlog::error("[main] run mapper fail");
        std::exit(EXIT_FAILURE);
    }

    spdlog::info("[main] Stop");

    return EXIT_SUCCESS;
}

void showSantax(char *argv[])
{
    printf("\n");
    printf("%s -c config -m mapData -s maxSessions -h\n", argv[0]);
    printf("    -c config: config file, default is .\\config.ini\n");
    printf("    -m mapData: sport:host:dport, for example: 8000:192.168.1.22:8000\n");
    printf("    -s maxSession: max client sessions, default is 1024.\n");
    printf("    -h show this help message.\n");
    printf("\n");
}

void init(int argc, char *argv[])
{
    try
    {
        // load config
        const char *cfgFile = getArg(argv, argv + argc, "-c");
        std::vector<std::string> argMapData;
        getArgMapData(argc, argv, "-m", argMapData);
        if (!gConf.parse(cfgFile ? cfgFile : CONFIG_FILE, argMapData))
        {
            perror("[init] load config file fail");
            std::exit(EXIT_FAILURE);
        }

        // get max sessions
        {
            const char *strSessions = getArg(argv, argv + argc, "-s");
            if (strSessions)
            {
                // read from args
                gSessions = strSessions ? atoi(strSessions) : 0;
            }
            else
            {
                std::string sRet = gConf.get("sessions", "global", "");
                // read from config file
                gSessions = atoi(sRet.c_str());
            }
            gSessions = gSessions < mapper::DEFAULT_SESSIONS ? mapper::DEFAULT_SESSIONS : gSessions;
        }

        // init logger
        {
            auto level = spdlog::level::from_str(gConf.get("level", "log", "info"));

            auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
            auto file_sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(LOG_FILE, true);
            spdlog::set_default_logger(
                std::make_shared<spdlog::logger>(
                    "mapper", spdlog::sinks_init_list({console_sink, file_sink})));
            spdlog::set_level(level);
            spdlog::flush_on(level);
        }
    }
    catch (std::exception &e)
    {
        perror("[init] catch an exception");
        std::exit(EXIT_FAILURE);
    }
}

bool hasArg(char **begin, char **end, const std::string &arg)
{
    return std::find(begin, end, arg) != end;
}

char *getArg(char **begin, char **end, const std::string &arg)
{
    char **itr = std::find(begin, end, arg);
    return itr != end && ++itr != end ? *itr : nullptr;
}

void getArgMapData(int argc, char *argv[], const char *arg, std::vector<std::string> &argMapData)
{
    for (int i = 1; i < argc - 1; ++i)
    {
        if (strcasecmp(argv[i], arg) == 0)
        {
            argMapData.emplace_back(argv[++i]);
        }
    }
}
