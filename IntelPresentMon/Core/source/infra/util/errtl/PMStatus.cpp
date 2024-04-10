// Copyright (C) 2022 Intel Corporation
// SPDX-License-Identifier: MIT
#include "PMStatus.h"
#include <format>
#include <Core/source/win/WinAPI.h>
#include <Core/Third/dxerr/dxerr.h>
#include <Core/source/infra/svc/Services.h>
#include <PresentMonAPI2/PresentMonAPI.h>

namespace p2c::infra::util::errtl
{
	namespace {
		PM_STATUS Get(const ErrorCode& code) { return std::any_cast<PM_STATUS>(code); }
	}

	void PMStatus::RegisterService()
	{
		infra::svc::Services::Singleton<infra::util::ErrorCodeTranslator>(
			typeid(PM_STATUS).name(),
			[] { return std::make_shared<infra::util::errtl::PMStatus>(); }
		);
	}

	std::wstring PMStatus::GetSymbol(const ErrorCode& code) const
	{
		if (auto pStrings = GetStrings_(Get(code))) {
			return pStrings->wideSymbol;
		}
		return L"";
	}

	std::wstring PMStatus::GetName(const ErrorCode& code) const
	{
		if (auto pStrings = GetStrings_(Get(code))) {
			return pStrings->wideName;
		}
		return L"";
	}

	std::wstring PMStatus::GetDescription(const ErrorCode& code) const
	{
		if (auto pStrings = GetStrings_(Get(code))) {
			return pStrings->wideDescription;
		}
		return L"";
	}

	const pmapi::EnumKeyStrings* PMStatus::GetStrings_(PM_STATUS statusCode) const noexcept
	{
		try {
			if (!pStatusMap_) {
				pStatusMap_ = pmapi::EnumMap::GetKeyMap(PM_ENUM_STATUS);
			}
			return &pStatusMap_->at(int(statusCode));
		}
		catch (...) {
			return nullptr;
		}
	}
}
