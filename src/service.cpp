#include "service.h"

namespace mapper
{

Service::Service(int srcPort,
                 std::string &targetAddress,
                 int targetPort,
                 SessionMgr &sessionMgr)
    : mSrcPort(srcPort),
      mTargetAddress(targetAddress),
      mTargetPort(targetPort),
      mSessionMgr(sessionMgr)
{
}

Service::~Service()
{
}

} // namespace mapper
