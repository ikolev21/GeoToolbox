// Copyright 2024 Ivan Kolev
//
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE.txt or copy at http://www.boost.org/LICENSE_1_0.txt)

#include "GeoToolbox/Config.hpp"

#include "GeoToolbox/StlExtensions.hpp"

#include <fstream>

using namespace std;

namespace
{
	pair<string_view, string_view> SplitKvp(string_view kvp)
	{
		auto const pos = kvp.find('=');
		if (pos == string_view::npos)
		{
			return {};
		}

		return { kvp.substr(0, pos), kvp.substr(pos + 1) };
	}
}

namespace GeoToolbox
{
	constexpr string_view CommentChars = ";#";

	void Config::AddKvp(std::string_view kvp, bool overwrite)
	{
		auto const [key, value] = SplitKvp(kvp);
		auto stringKey = string{ key };
		if (!value.empty() && !key.empty() && !Contains(CommentChars, key[0]) && (overwrite || values_.count(stringKey) == 0))
		{
			values_[stringKey] = value;
		}
	}

	void Config::AddCommandLine(Span<char* const> args, bool overwrite)
	{
		for (auto const arg : args)
		{
			AddKvp(arg, overwrite);
		}
	}

	void Config::ReadFile(filesystem::path const& filepath, bool overwrite)
	{
		ifstream file(filepath);
		string line;
		while (getline(file, line))
		{
			AddKvp(line, overwrite);
		}
	}

	std::string Config::GetValue(std::string const& key, std::string const& fallback)
	{
		auto const location = values_.find(key);
		if (location != values_.end())
		{
			return location->second;
		}

		auto value = GetEnvironmentVariable(key.c_str(), fallback);
		if (!value.empty())
		{
			values_.insert(pair{ key, value });
			return value;
		}

		return {};
	}

	int Config::GetValue(std::string const& key, int fallback)
	{
		auto const text = GetValue(key);
		if (!text.empty())
		{
			std::from_chars(text.data(), text.data() + text.length(), fallback);
		}

		return fallback;
	}
}
