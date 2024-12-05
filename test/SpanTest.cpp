#include "GeoToolbox/Span.hpp"

#include <catch2/catch_test_macros.hpp>

#include <regex>

#pragma warning( disable : 26496 )

//#define CONFIRM_COMPILATION_ERRORS

using namespace GeoToolbox;
using namespace std;

namespace
{
	struct BaseClass
	{
	};

	struct DerivedClass : BaseClass
	{
	};

#ifdef CONFIRM_COMPILATION_ERRORS
	struct DerivedClassLarger : BaseClass
	{
		int x;
	};
#endif
}

TEST_CASE("Span")
{
	SECTION("Default constructor")
	{
		{
			Span<int> s;
			REQUIRE((s.size() == 0 && s.data() == nullptr && s.empty()));

			Span<int const> cs;
			REQUIRE((cs.size() == 0 && cs.data() == nullptr));
		}

		{
			Span<int*> s;
			REQUIRE((s.size() == 0 && s.data() == nullptr));

			Span<int const> cs;
			REQUIRE((cs.size() == 0 && cs.data() == nullptr));
		}
	}

	SECTION("Size optimization")
	{
		{
			Span<int> s;
			REQUIRE(sizeof(s) == sizeof(int*) + sizeof(ptrdiff_t));
		}
	}

	SECTION("from_nullptr_size_constructor")
	{
		{
			Span<int> s{ nullptr, static_cast<Span<int>::index_type>(0) };
			REQUIRE((s.size() == 0 && s.data() == nullptr));

			Span<int const> cs{ nullptr, static_cast<Span<int>::index_type>(0) };
			REQUIRE((cs.size() == 0 && cs.data() == nullptr));
		}

		{
			[[maybe_unused]] auto workaround_macro = []
				{
					[[maybe_unused]] Span<int> s{ nullptr, 1 };
				};
			DEBUG_ONLY(REQUIRE_THROWS(workaround_macro()));

			[[maybe_unused]] auto const_workaround_macro = []
				{
					[[maybe_unused]] Span<int const> cs{ nullptr, 1 };
				};
			DEBUG_ONLY(REQUIRE_THROWS(const_workaround_macro()));
		}

		{
			Span<int*> s{ nullptr, static_cast<Span<int>::index_type>(0) };
			REQUIRE((s.size() == 0 && s.data() == nullptr));

			Span<int const*> cs{ nullptr, static_cast<Span<int>::index_type>(0) };
			REQUIRE((cs.size() == 0 && cs.data() == nullptr));
		}
	}

	SECTION("from_pointer_size_constructor")
	{
		int arr[4] = { 1, 2, 3, 4 };

		{
			Span s{ &arr[0], 2 };
			REQUIRE((s.size() == 2 && s.data() == &arr[0]));
			REQUIRE((s[0] == 1 && s[1] == 2));
		}

		{
			int* p = nullptr;
			Span<int> s{ p, nullptr };
			REQUIRE((s.size() == 0 && s.data() == nullptr));
		}

		{
			int* p = nullptr;
			[[maybe_unused]] auto workaround_macro = [=]
				{
					[[maybe_unused]] Span s{ p, 2 };
				};
			DEBUG_ONLY(REQUIRE_THROWS(workaround_macro()));
		}
	}

	SECTION("from_pointer_pointer_constructor")
	{
		int arr[4] = { 1, 2, 3, 4 };

		{
			Span s{ &arr[0], &arr[2] };
			REQUIRE((s.size() == 2 && s.data() == &arr[0]));
			REQUIRE((s[0] == 1 && s[1] == 2));
		}

		{
			Span s{ &arr[0], &arr[0] };
			REQUIRE((s.size() == 0 && s.data() == &arr[0]));
		}

		{
			[[maybe_unused]] auto workaround_macro = [&]
				{
					[[maybe_unused]] Span s{ &arr[1], &arr[0] };
				};
			DEBUG_ONLY(REQUIRE_THROWS(workaround_macro()));
		}

		{
			int* p = nullptr;
			[[maybe_unused]] auto workaround_macro = [&]
				{
					[[maybe_unused]] Span s{ &arr[0], p };
				};
			DEBUG_ONLY(REQUIRE_THROWS(workaround_macro()));
		}

		{
			int* p = nullptr;
			Span s{ p, p };
			REQUIRE((s.size() == 0 && s.data() == nullptr));
		}

		{
			int* p = nullptr;
			[[maybe_unused]] auto workaround_macro = [&]
				{
					[[maybe_unused]] Span s{ &arr[0], p };
				};
			DEBUG_ONLY(REQUIRE_THROWS(workaround_macro()));
		}
	}

	SECTION("from_array_constructor")
	{
		int arr[5] = { 1, 2, 3, 4, 5 };

		{
			Span s{ arr };
			REQUIRE((s.size() == 5 && s.data() == &arr[0]));
		}

		int arr2d[2][3] = { { 1, 2, 3 }, { 4, 5, 6 } };

		{
			Span s{ &(arr2d[0]), 1 };
			REQUIRE((s.size() == 1 && s.data() == &arr2d[0]));
		}

		int arr3d[2][3][2] = { { { 1, 2 }, { 3, 4 }, { 5, 6 } }, { { 7, 8 }, { 9, 10 }, { 11, 12 } } };

		{
			Span s{ &arr3d[0], 1 };
			REQUIRE((s.size() == 1 && s.data() == &arr3d[0]));
		}
	}

	SECTION("from_dynamic_array_constructor")
	{
		double(*arr)[3][4] = new double[100][3][4];

		{
			Span s{ &arr[0][0][0], 10 };
			REQUIRE((s.size() == 10 && s.data() == &arr[0][0][0]));
		}

		delete[] arr;
	}

	SECTION("from_std_array_constructor")
	{
		std::array<int, 4> arr = { 1, 2, 3, 4 };

		{
			Span<int> s{ arr };
			REQUIRE((s.size() == int(arr.size()) && s.data() == arr.data()));

			Span<int const> cs{ arr };
			REQUIRE((cs.size() == int(arr.size()) && cs.data() == arr.data()));
		}

		{
			auto get_an_array = []()->std::array<int, 4> { constexpr std::array<int, 4> x = { 1, 2, 3, 4 }; return x; };
			auto take_a_span = [](Span<int const>)
				{
				};
			// try to take a temporary std::array
			take_a_span(get_an_array());
		}
	}

	SECTION("from_const_std_array_constructor")
	{
		constexpr std::array<int, 4> arr = { 1, 2, 3, 4 };

		{
			Span<int const> s{ arr };
			REQUIRE(s.size() == int(arr.size()));
			REQUIRE(s.data() == arr.data());
		}

		{
			auto get_an_array = []() -> std::array<int, 4> const { constexpr std::array<int, 4> x = { 1, 2, 3, 4 }; return x; };
			auto take_a_span = [](Span<int const>)
				{
				};
			// try to take a temporary std::array
			take_a_span(get_an_array());
		}
	}

	SECTION("from_std_array_const_constructor")
	{
		std::array<int const, 4> arr = { 1, 2, 3, 4 };

		{
			Span<int const> s{ arr };
			REQUIRE(s.size() == int(arr.size()));
			REQUIRE(s.data() == arr.data());
		}
	}

	SECTION("from_container_constructor")
	{
		std::vector<int> v;
		v.push_back(1); v.push_back(2); v.push_back(3);

		{
			Span<int> s{ v };
			REQUIRE(s.size() == int(v.size()));
			REQUIRE(s.data() == v.data());

			Span<int const> cs(v);
			REQUIRE(cs.size() == int(v.size()));
			REQUIRE(cs.data() == v.data());
		}

		auto const cv = v;
		{
			Span<int const> cs(cv);
			REQUIRE(cs.size() == int(cv.size()));
			REQUIRE(cs.data() == cv.data());
		}

		std::string str = "hello";
		std::string const cstr = "hello";

		{
			Span<char> s{ str };
			REQUIRE(s.size() == int(str.size()));
			REQUIRE(s.data() == str.data());

			Span<char const> cs(str);
			REQUIRE(cs.size() == int(str.size()));
			REQUIRE(cs.data() == str.data());
		}

		{
#ifdef CONFIRM_COMPILATION_ERRORS
			Span<char> s{ cstr };
#endif
			Span<char const> cs(cstr);
			REQUIRE(cs.size() == int(cstr.size()));
			REQUIRE(cs.data() == cstr.data());
		}

		{
#ifdef CONFIRM_COMPILATION_ERRORS
			auto get_temp_vector = []() -> std::vector<int>
				{
					return{};
				};
			auto use_span = [](Span<int>)
				{
				};
			use_span(get_temp_vector());
#endif
		}

		{
			auto get_temp_vector = []() -> std::vector<int>
				{
					return std::vector<int>{};
				};
			auto use_span = [](Span<int const>)
				{
				};
			use_span(get_temp_vector());
		}

		{
#ifdef CONFIRM_COMPILATION_ERRORS
			auto get_temp_string = []() -> std::string
				{
					return{};
				};
			auto use_span = [](Span<char>)
				{
				};
			use_span(get_temp_string());
#endif
		}

		{
			auto get_temp_string = []() -> std::string
				{
					return "";
				};
			auto use_span = [](Span<char const>)
				{
				};
			use_span(get_temp_string());
		}

		{
#ifdef CONFIRM_COMPILATION_ERRORS
			auto get_temp_vector = []() -> const std::vector<int>
				{
					return{};
				};
			auto use_span = [](Span<const char>)
				{
				};
			use_span(get_temp_vector());
#endif
		}

		{
			auto get_temp_string = []() -> std::string const
			{
					return "";
				};
			auto use_span = [](Span<char const>)
				{
				};
			use_span(get_temp_string());
		}

		{
#ifdef CONFIRM_COMPILATION_ERRORS
			std::map<int, int> m;
			Span<int> s{ m };
#endif
		}
	}

	SECTION("from_convertible_span_constructor")
	{
		{
			Span<DerivedClass> avd;
			[[maybe_unused]] Span<DerivedClass const> avcd = avd;
		}

		{
			Span<DerivedClass> avd;
			[[maybe_unused]] Span<BaseClass> avb = avd;
		}

		{
#ifdef CONFIRM_COMPILATION_ERRORS
			Span<DerivedClassLarger> avd;
			[[maybe_unused]] Span<BaseClass> avb = avd;
#endif
		}

#ifdef CONFIRM_COMPILATION_ERRORS
		{
			Span<int> s;
			[[maybe_unused]] Span<unsigned int> s2 = s;
		}

		{
			Span<int> s;
			[[maybe_unused]] Span<const unsigned int> s2 = s;
		}

		{
			Span<int> s;
			[[maybe_unused]] Span<short> s2 = s;
		}
#endif
	}

	SECTION("copy_move_and_assignment")
	{
		Span<int> s1;
		REQUIRE(s1.empty());

		int arr[] = { 3, 4, 5 };

		Span<int const> s2 = arr;
		REQUIRE(s2.size() == 3);
		REQUIRE(s2.data() == &arr[0]);

		s2 = s1;
		REQUIRE(s2.empty());

		auto get_temp_span = [&]() -> Span<int>
			{
				return Span{ &arr[1], 2 };
			};
		auto use_span = [&](Span<int const> s)
			{
				REQUIRE(s.size() == 2);
				REQUIRE(s.data() == &arr[1]);
			};
		use_span(get_temp_span());

		s1 = get_temp_span();
		REQUIRE(s1.size() == 2);
		REQUIRE(s1.data() == &arr[1]);
	}

	SECTION("subspan")
	{
		int arr[5] = { 1, 2, 3, 4, 5 };

		{
			Span<int> av;
			REQUIRE(av.subspan(0, 0).size() == 0);
			REQUIRE(av.subspan(0, 0).size() == 0);
		}

		{
			Span<int> av;
			REQUIRE(av.subspan(0).size() == 0);
			DEBUG_ONLY(REQUIRE_THROWS(av.subspan(1).size()));
		}

		{
			Span av = arr;
			REQUIRE(av.subspan(0).size() == 5);
			REQUIRE(av.subspan(1).size() == 4);
			REQUIRE(av.subspan(4).size() == 1);
			REQUIRE(av.subspan(5).size() == 0);
			DEBUG_ONLY(REQUIRE_THROWS(av.subspan(6).size()));
			auto const av2 = av.subspan(1);
			for ( auto i = 0; i < 4; ++i)
			{
				REQUIRE( av2[i] == i + 2 );
			}
		}
	}

	SECTION("begin_end")
	{
		{
			int a[] = { 1, 2, 3, 4 };
			Span s = a;

			auto it = s.begin();
			auto it2 = std::begin(s);
			REQUIRE(it == it2);

			it = s.end();
			it2 = std::end(s);
			REQUIRE(it == it2);
		}

		{
			int a[] = { 1, 2, 3, 4 };
			Span s = a;

			auto it = s.begin();
			auto first = it;
			REQUIRE(it == first);
			REQUIRE(*it == 1);

			auto beyond = s.end();
			REQUIRE(it != beyond);
			//REQUIRE_ASSERTFAILS( 1, 0, *beyond );

			REQUIRE(beyond - first == 4);
			REQUIRE(first - first == 0);
			REQUIRE(beyond - beyond == 0);

			++it;
			REQUIRE(it - first == 1);
			REQUIRE(*it == 2);
			*it = 22;
			REQUIRE(*it == 22);
			REQUIRE(beyond - it == 3);

			it = first;
			REQUIRE(it == first);
			while (it != s.end())
			{
				*it = 5;
				++it;
			}

			REQUIRE(it == beyond);
			REQUIRE(it - beyond == 0);

			REQUIRE(std::all_of(s.begin(), s.end(), [](int x)
				{
					return x == 5;
				}));
		}
	}

	SECTION("interop_with_std_regex")
	{
		char lat[] = { '1', '2', '3', '4', '5', '6', 'E', 'F', 'G' };
		Span s = lat;
		auto const f_it = s.begin() + 7;

		std::match_results<Span<char>::iterator> match;

		std::regex_match(s.begin(), s.end(), match, std::regex(".*"));
		REQUIRE(match.ready());
		REQUIRE(!match.empty());
		REQUIRE(match[0].matched);
		REQUIRE(match[0].first == s.begin());
		REQUIRE(match[0].second == s.end());

		std::regex_search(s.begin(), s.end(), match, std::regex("F"));
		REQUIRE(match.ready());
		REQUIRE(!match.empty());
		REQUIRE(match[0].matched);
		REQUIRE(match[0].first == f_it);
		REQUIRE(match[0].second == (f_it + 1));
	}

	SECTION("default_constructible")
	{
		REQUIRE((std::is_default_constructible<Span<int>>::value));
	}
}
