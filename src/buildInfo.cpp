/**
 * @file buildInfo.cpp
 * @author Liu Yu (source@liuyu.com)
 * @brief program for update build information, such as
 *          build since time
 *          last build time
 *          total build count
 * @version 1
 * @date 2019-11-07
 * 
 * @copyright Copyright (c) 2019
 * 
 */

#define _XOPEN_SOURCE
#include <string.h>
#include <time.h>
#include <sys/time.h>
#include <chrono>
#include <fstream>
#include <iostream>
#include "config.h"

using namespace std;
using namespace mapper;

bool hasArg(char **begin, char **end, const std::string &arg);
time_t currentTime();
time_t parseTime(string &strTime);
string timeToStr(time_t time);

int main(int argc, char *argv[])
{
    Config conf;

    if (argc < 2)
    {
        cerr << "invalid args" << endl;
        return 1;
    }

    // parse build info file
    conf.parse(argv[1], true);

    // current time
    time_t curTime = currentTime();

    // parse 'build since time'
    string sinceStr = conf.get("since", "build");
    time_t sinceTime = parseTime(sinceStr);
    sinceTime = sinceTime ? sinceTime : curTime;

    // parse build count
    string countStr = conf.get("count", "build");
    int count = atoi(countStr.c_str());
    count = count > 0 ? count + 1 : 1;

    if (hasArg(argv, argv + argc, "-since"))
    {
        cout << timeToStr(sinceTime);
        return 0;
    }
    else if (hasArg(argv, argv + argc, "-last"))
    {
        cout << conf.get("last", "build", timeToStr(curTime));
        return 0;
    }
    else if (hasArg(argv, argv + argc, "-count"))
    {
        cout << count;
        return 0;
    }

    // update file
    {
        ofstream ofs(argv[1]);
        if (!ofs.is_open())
        {
            cerr << "update build info fail" << endl;
            return 1;
        }

        ofs << "[build]" << endl
            << "since=" << timeToStr(sinceTime) << endl
            << "last=" << timeToStr(curTime) << endl
            << "count=" << count << endl;

        ofs.flush();
        ofs.close();
    }

    return 0;
}

bool hasArg(char **begin, char **end, const std::string &arg)
{
    return std::find(begin, end, arg) != end;
}

time_t currentTime()
{
    struct timeval tv;
    gettimeofday(&tv, nullptr);

    return tv.tv_sec;
}

time_t parseTime(string &strTime)
{
    struct tm tm;
    memset(&tm, 0, sizeof(struct tm));

    return strptime(strTime.c_str(), "%Y%m%d%H%M%S", &tm) == nullptr ? 0 : mktime(&tm);
}

string timeToStr(time_t time)
{
    struct tm *ptm = localtime(&time);
    char buffer[32];
    strftime(buffer, 32, "%Y%m%d%H%M%S", ptm);

    return buffer;
}
