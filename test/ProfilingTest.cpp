// Copyright 2024 Ivan Kolev
//
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE.txt or copy at http://www.boost.org/LICENSE_1_0.txt)

#include "GeoToolbox/Asserts.hpp"
#include "GeoToolbox/DescribeStruct.hpp"
#include "GeoToolbox/Profiling.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <iostream>

using namespace std;

TEST_CASE("Timing")
{
	using namespace GeoToolbox;
	Timings timings{ 100'000 };

	SECTION("Multiple repeats of a single action")
	{
		auto sinX = 0.0;
		while (timings.NextIteration())
		{
			auto x = 0.6;
			sinX = timings.Record("sin", 1'000'000, [&x] { return sin(DoNotOptimize(x)); });
		}

		cout << timings.Print();

		REQUIRE(sinX > 0);
		RELEASE_ONLY(REQUIRE(timings.TotalRunningTime() >= timings.MinimumRunningTime()));
	}

	SECTION("Multiple actions")
	{
		vector<int> data(1'000'000);

		auto n = 0;
		while (timings.NextIteration())
		{
			timings.Record(
				"iota",
				[&data]
				{
					iota(data.rbegin(), data.rend(), 1);
				});

			n = timings.Record(
				"sort",
				[&data]
				{
					sort(data.begin(), data.end());
					return data.front();
				});
		}

		cout << timings.Print();

		REQUIRE(n == 1);
		RELEASE_ONLY(REQUIRE(timings.TotalRunningTime() >= timings.MinimumRunningTime()));
		REQUIRE(timings.GetAllActions().size() == 2);
	}
}

struct X
{
	int i;
	double d;
	string s;
};

template <>
constexpr auto GeoToolbox::DescribeStruct<X>()
{
	return tuple{
		Field{ &X::i, "Int" },
		Field{ &X::d, "Double" },
		Field{ &X::s, "String" } };
}

struct X2
{
	int i;
	string s;
	float f;

	static constexpr auto DescribeStruct()
	{
		using GeoToolbox::Field;
		return tuple{
			Field{ &X2::i, "Int" },
			Field{ &X2::f, "Float" },
			Field{ &X2::s, "String" } };
	}
};

using namespace GeoToolbox;

TEST_CASE("DescribeStruct")
{
	X x = { 13, 17.0, "asd" };

	constexpr auto descriptorX = DescribeStruct<X>();
	STATIC_REQUIRE(is_same_v<DescriptorTuple<X>, tuple<Field<X, int>, Field<X, double>, Field<X, string>>>);
	STATIC_REQUIRE(is_same_v<ValueTuple<X>, tuple<int, double, string>>);
	STATIC_REQUIRE(GetFieldNames<X>()[1] == "Double");
	STATIC_REQUIRE(get<2>(descriptorX).name == "String"sv);
	REQUIRE(get<1>(AsTuple(x)) == 17.0);

	stringstream s;
	WriteFieldNames<X>(s);
	REQUIRE(s.str() == "Int\tDouble\tString");

	s.str("");
	WriteStruct(s, x);
	REQUIRE(s.str() == "13\t17\tasd");

	s.str("");
	TupleForEach(AsTuple(X{ x }), [&s](auto&& field)
		{
			s << field << ' ';
		});
	REQUIRE(s.str() == "13 17 asd ");

	X xread = {};
	ReadStruct(s, xread);
	REQUIRE(xread.d == x.d);
	REQUIRE(xread.s == x.s);

	REQUIRE(AsTuple(xread) == AsTuple(x));

	X2 x2 = { -1, "", -2 };
	CopyStruct(x, x2);
	REQUIRE(x2.i == x.i);
	REQUIRE(x2.s == x.s);
}


TEST_CASE("IntegerRange")
{
	auto sum = 0;
	for (auto i : MakeRange( 1, 4 ))
	{
		sum += i;
	}

	REQUIRE(sum == 6);

	array arr{ 1, 2, 3 };

	sum = 0;
	for (auto i : MakeRange( arr.begin(), arr.end() ))
	{
		sum += *i;
	}

	REQUIRE(sum == 6);
}
