#include "Exception.h"
#include "EnumMap.h"
#include <format>

namespace pmapi
{
    ApiErrorException::ApiErrorException(PM_STATUS err, const std::string& message)
        :
        Exception{ BuildWhatString_(err, message) },
        errorCode_{ err }
    {}

    std::string ApiErrorException::BuildWhatString_(PM_STATUS err, const std::string& message)
    {
        try {
            auto pStatusMap = EnumMap::GetKeyMap(PM_ENUM_STATUS);
            auto& str = pStatusMap->at(err);
            return std::format("{} | Error: {} ({}) {}: {}", message,
                str.narrowSymbol, int(err), str.narrowName, str.narrowDescription);
        }
        catch (...) {
            return std::format("{} | Unknown error code [{}]", message, int(err));
        }
    }
}