#pragma once

namespace pmon::util
{
	inline constexpr size_t GetPadding(size_t byteIndex, size_t alignment)
	{
		const auto partialBytes = byteIndex % alignment;
		const auto padding = (alignment - partialBytes) % alignment;
		return padding;
	}

	template<typename T>
	constexpr size_t GetPadding(size_t byteIndex)
	{
		return GetPadding(byteIndex, alignof(T));
	}
}