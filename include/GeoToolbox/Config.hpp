// Copyright 2024 Ivan Kolev
//
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE.txt or copy at http://www.boost.org/LICENSE_1_0.txt)

#pragma once

#include "GeoToolbox/Span.hpp"

#include <filesystem>
#include <map>
#include <string>

namespace GeoToolbox
{
	class Config
	{
		std::map<std::string, std::string> values_;

	public:

		void AddKvp(std::string_view kvp, bool overwrite = true);

		void AddCommandLine(Span<char* const> args, bool overwrite = true);

		void ReadFile(std::filesystem::path const&, bool overwrite = true);

		std::string GetValue(std::string const& key, std::string const& fallback = {});

		int GetValue(std::string const& key, int fallback);
	};
}
