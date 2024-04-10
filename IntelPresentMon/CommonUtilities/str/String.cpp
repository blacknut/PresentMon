// Copyright (C) 2022 Intel Corporation
// SPDX-License-Identifier: MIT
#include "String.h"
#include <sstream>
#include <iomanip>
// TODO: replace with with properly wrapped winapi include
#include <Windows.h>

namespace pmon::util::str
{
	std::vector<std::string> TokenizeQuoted(const std::string& input)
	{
		std::istringstream stream;
		stream.str(input);
		std::vector<std::string> tokens;
		std::string token;

// okay to move from token and use because std::quoted(token) fills the string and does not read from
#pragma warning(push)
#pragma warning(disable : 26800)
		while (stream >> std::quoted(token))
		{
			tokens.push_back(std::move(token));
		}
#pragma warning(pop)
		return tokens;
	}

	std::wstring ToWide(const std::string& narrow)
	{
		if (narrow.empty()) {
			return {};
		}
		std::wstring wide;
		// TODO: replace with resize_and_overwrite when it becomes widely available
		wide.resize(narrow.size() + 1);
		const auto actual = MultiByteToWideChar(CP_UTF8, 0, narrow.data(), (int)narrow.size(), wide.data(), (int)wide.size());
		if (actual > 0) {
			wide.resize(actual);
			return wide;
		}
		// TODO: log error here
		return {};
	}

	std::string ToNarrow(const std::wstring& wide)
	{
		std::string narrow;
		// TODO: replace with resize_and_overwrite when it becomes widely available
		narrow.resize(wide.size() * 2);
		const auto actual = WideCharToMultiByte(CP_UTF8, 0, wide.data(), (int)wide.size(),
			narrow.data(), (int)narrow.size(), nullptr, nullptr);
		if (actual > 0) {
			narrow.resize(actual);
			return narrow;
		}
		// TODO: (maybe) check for insufficient buffer error and do redo with two-pass (or just double buffer again)
		// TODO: log error here
		return {};
	}
}