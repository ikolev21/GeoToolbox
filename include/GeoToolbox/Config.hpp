// Copyright 2024-2026 Ivan Kolev
//
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE.txt or copy at http://www.boost.org/LICENSE_1_0.txt)

#pragma once

#include "GeoToolbox/Span.hpp"

#include <charconv>
#include <filesystem>
#include <map>
#include <string>
#include <variant>
#include <vector>

namespace GeoToolbox
{
	template <typename T>
	std::string ToString(T const& value)
	{
		std::string result;
		if constexpr (std::is_convertible_v<T, std::string>)
		{
			result = std::string(value);
		}
		else
		{
			result = std::to_string(value);
		}

		return result;
	}

	using ConfigValueType = std::variant<bool, int, double, std::string>;

	struct ConfigKeyDesc
	{
		std::string key;
		ConfigValueType defaultValue;
		std::string description;
	};

	template <class TVariant>
	std::type_info const& GetVariantType(TVariant const& v)
	{
		return std::visit([]([[maybe_unused]] auto&& x)->decltype(auto) { return typeid(x); }, v);
	}

	inline void Output(std::ostream& stream, ConfigValueType const& value)
	{
		std::visit([&stream](auto const& x) { stream << x; }, value);
	}

	class Config
	{
		std::map<std::string, std::string> values_;
		std::vector<ConfigKeyDesc> descs_;

	public:
		
		Config() = default;

		Config(std::initializer_list<std::pair<std::string const, std::string>> kvps)
			: values_( kvps )
		{
		}

		// kvp is "key=value"
		void AddKeyValuePair(std::string_view kvp, bool overwrite = true);

		template <typename T>
		void Insert(std::string const& key, T value)
		{
			values_.insert(std::pair{ key, ToString(value) });
		}

		template <typename T>
		void InsertOrAssign(std::string key, T value)
		{
			values_.insert_or_assign(std::move(key), ToString(value));
		}

		void InsertOrAssign(std::initializer_list<std::pair<std::string, std::string>> kvps)
		{
			for (auto&& [key, value] : kvps)
			{
				values_.insert_or_assign(std::move(key), std::move(value));
			}
		}

		void AddCommandLine(Span<char const* const> args, bool overwrite = true);

		void ReadFile(std::filesystem::path const&, bool overwrite = true);

		void RegisterKeys(std::vector<ConfigKeyDesc> descs);

		std::string GenerateDefaultConfigFile() const;


		// Value must be registered in advance
		template <typename T>
		T Get(std::string const& key)
		{
			auto const descIter = find_if(descs_.begin(), descs_.end(), [&key](ConfigKeyDesc const& desc) { return desc.key == key; });
			ASSERT(descIter != descs_.end(), "key not registered: ", key);
			auto text = GetString(key, "");
			ASSERT(std::holds_alternative<T>(descIter->defaultValue), "key retrieved with wrong type, actual type: ", GetVariantType(descIter->defaultValue).name());
			T result;
			if constexpr (std::is_same_v<T, bool>)
			{
				result = !text.empty() ? text == "1" : std::get<T>(descIter->defaultValue);
			}
			else if constexpr (std::is_same_v<T, int> || std::is_same_v<T, double>)
			{
				if (text.empty() || std::from_chars(text.data(), text.data() + text.length(), result).ec != std::errc{})
				{
					result = std::get<T>(descIter->defaultValue);
				}
			}
			else
			{
				result = !text.empty() ? std::move(text) : std::get<std::string>(descIter->defaultValue);
			}

			return result;
		}

		int GetInt(std::string const& key, int fallback);

		std::string GetString(std::string const& key, std::string const& fallback);

		bool GetBool(std::string const& key, bool fallback)
		{
			return GetInt(key, fallback ? 1 : 0) != 0;
		}
	};
}
