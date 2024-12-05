/*
Primitive struct introspection.

Provides conversion to tuple and field name access:
GetFieldNames<TStruct>(), AsTuple(structValue), WriteFieldNames<TStruct>(stream), WriteStruct(stream, structValue), ReadStruct(stream, structValue)

Works for structs with a static DescribeStruct() method or a specialization of GeoToolbox::DescribeStruct<T>() like this:

struct X
{
	int i;
	double d;

	// Either

	static constexpr auto DescribeStruct()
	{
		using GeoToolbox::Field;

		return std::tuple{
			Field{ &X::i, "Int" },
			Field{ &X::d, "Double" } };
	}
};

// or

template <>
constexpr auto GeoToolbox::DescribeStruct<X>()
{
	return std::tuple{
		Field{ &X::i, "Int" },
		Field{ &X::d, "Double" } };
}
*/

#pragma once

#include "GeoToolbox/StlExtensions.hpp"

#include <array>
#include <ostream>

namespace GeoToolbox
{
	template <class TStruct, typename TField>
	struct Field
	{
		using type = TField;

		TField TStruct::* fieldPointer;
		std::string_view name;
	};

	template <class TStruct, typename TField>
	Field(TField TStruct::* p, std::string_view n) -> Field<TStruct, TField>;

	template <class TStruct>
	constexpr auto DescribeStruct()
	{
		return TStruct::DescribeStruct();
	}

	template <class TField>
	struct GetFieldType
	{
		//using type = std::remove_pointer_t<decltype(T::value)>;
		using type = typename TField::type;
	};

	template <class TStruct>
	using DescriptorTuple = decltype(DescribeStruct<TStruct>());

	template <class TStruct>
	using ValueTuple = TypeListMap<DescriptorTuple<TStruct>, GetFieldType>;

	namespace Detail
	{
		template <class T, std::size_t... Indices>
		constexpr auto GetFieldNamesImpl(T const& descriptor, std::index_sequence<Indices...>)
		{
			return std::array<std::string_view, sizeof...(Indices)>{ std::get<Indices>(descriptor).name... };
		}

		template <class TStruct, class TDesc, std::size_t... Indices>
		constexpr auto AsTupleImpl(TStruct&& value, TDesc const& descriptor, std::index_sequence<Indices...>)
		{
			return std::make_tuple(std::ref(value.*std::get<Indices>(descriptor).fieldPointer)...);
		}
	}

	template <class TStruct>
	constexpr auto GetFieldNames()
	{
		auto const descriptor = DescribeStruct<TStruct>();
		return Detail::GetFieldNamesImpl(descriptor, std::make_index_sequence<std::tuple_size_v<decltype(descriptor)>>());
	}

	template <class TStruct>
	constexpr auto AsTuple(TStruct&& x)
	{
		auto const descriptor = DescribeStruct<std::decay_t<TStruct>>();
		return Detail::AsTupleImpl(std::forward<TStruct>(x), descriptor, std::make_index_sequence<std::tuple_size_v<decltype(descriptor)>>());
	}

	template <class TStructSource, class TStructDest>
	constexpr auto CopyStruct(TStructSource const& source, TStructDest& dest)
	{
		constexpr auto descSource = DescribeStruct<TStructSource>();
		constexpr auto descDest = DescribeStruct<TStructDest>();
		TupleForEach(descSource, [&]([[maybe_unused]] auto& sourceFieldDesc)
			{
				TupleForEach(descDest, [&](auto& destFieldDesc)
					{
						if constexpr (std::is_same_v<typename std::decay_t<decltype(sourceFieldDesc)>::type, typename std::decay_t<decltype(destFieldDesc)>::type>)
						{
							if (sourceFieldDesc.name == destFieldDesc.name)
							{
								dest.*destFieldDesc.fieldPointer = source.*sourceFieldDesc.fieldPointer;
							}
						}
					});
			});
	}

	template <class TStruct>
	void WriteFieldNames(std::ostream& out, char separator = '\t')
	{
		auto const fieldNames = GetFieldNames<TStruct>();
		out << fieldNames[0];
		for (auto i = 1U; i < fieldNames.size(); ++i)
		{
			out << separator << fieldNames[i];
		}
	}

	template <class TStruct>
	void WriteStruct(std::ostream& out, TStruct const& value, char separator = '\t')
	{
		TupleForEach(AsTuple(value), [&out, separator, columnIndex = 0](auto& field) mutable
			{
				if (columnIndex > 0)
				{
					out << separator;
				}

				out << field;
				++columnIndex;
			});
	}

	template <class TStruct>
	void ReadStruct(std::istream& in, TStruct& value)
	{
		auto tuple = AsTuple(value);
		TupleForEach(tuple, [&in](auto& field)
			{
				in >> field;
			});
	}

	template <class TStruct>
	void ReadStruct(std::istream& in, TStruct& value, StringStorage& storage)
	{
		auto tuple = AsTuple(value);
		TupleForEach(tuple, [&in, &storage](auto& field)
			{
				if constexpr (std::is_same_v<std::decay_t<decltype(field)>, std::string_view>)
				{
					std::string temp;
					in >> temp;
					field = storage.GetOrAddString(temp);
				}
				else
				{
					in >> field;
				}
			});
	}
}
