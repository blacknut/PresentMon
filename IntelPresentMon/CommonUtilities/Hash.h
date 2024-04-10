// Copyright (C) 2022 Intel Corporation
// SPDX-License-Identifier: MIT
#pragma once
#include <utility>


namespace pmon::util::hash
{
	size_t HashCombine(size_t lhs, size_t rhs);

	template <typename T, typename S>
	size_t DualHash(const T& t, const S& s)
	{
		return HashCombine(std::hash<T>{}(t), std::hash<S>{}(s));
	}
}

namespace std
{
	template <class T, class S>
	struct hash<std::pair<T, S>>
	{
		size_t operator()(const std::pair<T, S>& p) const
		{
			return pmon::util::hash::DualHash(p.first, p.second);
		}
	};
}