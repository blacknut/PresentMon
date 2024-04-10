#include "Exception.h"

namespace pmon::mid
{
	Exception::Exception(PM_STATUS errorCode)
		:
		errorCode_{ errorCode }
	{}
	PM_STATUS Exception::GetErrorCode() const
	{
		return errorCode_;
	}
	const char* Exception::what() const
	{
		using namespace std::string_literals;
		if (whatBuffer_.empty()) {
			whatBuffer_ = "Middleware error with code: "s + std::to_string((int)errorCode_);
		}
		return whatBuffer_.c_str();
	}
}