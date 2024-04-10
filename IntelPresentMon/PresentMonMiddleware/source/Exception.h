#pragma once
#include <stdexcept>
#include <string>
#include "../../PresentMonAPI2/PresentMonAPI.h"

namespace pmon::mid
{
	class Exception : public std::exception
	{
	public:
		Exception(PM_STATUS errorCode);
		virtual PM_STATUS GetErrorCode() const;
		const char* what() const override;
	private:
		PM_STATUS errorCode_;
		mutable std::string whatBuffer_;
	};
}