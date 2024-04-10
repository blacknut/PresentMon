#include "CliFramework.h"
#include "../str/String.h"
#include <span>
#include <vector>

namespace pmon::util::cli
{
	OptionsContainer::OptionsContainer(const char* description, const char* name) : app_{ description, name } {}
	std::string OptionsContainer::GetName() const
	{
		return app_.get_name();
	}
	void OptionsContainer::Finalize_(int argc, const char* const* argv)
	{
		app_.parse(argc, argv);
		finalized_ = true;
	}
	int OptionsContainer::Exit_(const CLI::ParseError& e)
	{
		return app_.exit(e);
	}

	OptionsContainer::ConvertedNarrowOptions_::ConvertedNarrowOptions_(int argc, const wchar_t* const* wargv)
	{
		for (auto pWideStr : std::span{ wargv, size_t(argc) }) {
			const auto narrow = str::ToNarrow(pWideStr);
			auto pNarrowStr = new char[narrow.size() + 1];
			strcpy_s(pNarrowStr, narrow.size() + 1, narrow.c_str());
			stringPointerArray.push_back(pNarrowStr);
		}
	}
	OptionsContainer::ConvertedNarrowOptions_::~ConvertedNarrowOptions_()
	{
		for (auto p : stringPointerArray) {
			delete[] p;
		}
	}
	const char* const* OptionsContainer::ConvertedNarrowOptions_::GetRawPointerArray() const
	{
		return stringPointerArray.data();
	}

	Flag::Flag(OptionsContainer* pParent, std::string names, std::string description)
	{
		pOption_ = pParent->app_.add_flag(std::move(names), data_, std::move(description));
	}
	bool Flag::operator*() const
	{
		return bool(*this);
	}
	Flag::operator bool() const
	{
		return (bool)*pOption_;
	}
	bool Flag::operator!() const
	{
		return !bool(*this);
	}
}