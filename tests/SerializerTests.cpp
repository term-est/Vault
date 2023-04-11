#include <ranges>
#include <tuple>

#include <gtest/gtest.h>
#include <cereal/archives/binary.hpp>

#include "Serializer.hpp"


template <typename T>
concept BufferType = requires
{
	requires std::same_as<T, std::byte> || std::same_as<T, char> || std::same_as<T, std::uint8_t> || std::same_as<T, char8_t> || std::same_as<T, std::int8_t>;
};

[[nodiscard]]
constexpr bool operator==(BufferType auto e, std::byte byte) noexcept
{
	return static_cast<std::byte>(e) == byte;
}

[[nodiscard]]
constexpr bool operator==(std::byte byte, BufferType auto e) noexcept
{
	return e == byte;
}

template <typename T>
std::string_view type_name()
{
	constexpr auto prefix   = std::string_view{"[with T = "};
	constexpr auto suffix   = std::string_view{";"};
	constexpr auto function = std::string_view{__PRETTY_FUNCTION__};

	constexpr auto start = function.find(prefix) + prefix.size();
	constexpr auto end = function.rfind(suffix);

	static_assert(start < end);

	constexpr auto result = function.substr(start, (end - start));

	return result;
}


template <typename Tuple>
struct typelist_from_tuple
{
	using type = decltype(
	[] <typename ... Ts> (std::tuple<Ts...>)
	{
		return ::testing::Types<Ts...>{};

	}(std::declval<Tuple>()));
};



class TestHelper : public testing::Test
{
protected:

	template <typename ... Ts>
	bool call_helper(std::tuple<Ts...>& data, auto Function) const
	{
		return
		[&] <std::size_t ... idx> (std::index_sequence<idx...>)
		{
			auto helper =
			[&, called = false] <std::size_t i>() mutable
			{
				if (called)
					return called;

				if constexpr (requires { Function(std::get<i>(data)); })
				{
					Function(std::get<i>(data));
					called = true;
				}

				return called;
			};

			return (helper.template operator()<idx>(), ...);
		}(std::index_sequence_for<Ts...>{});
	}
};

template <typename T>
class PrimitiveSuite : public TestHelper
{

	bool primitive_helper(auto data)
	{
		auto ret = MILI::serialize(data);

		// serialize with cereal
		std::stringstream ss;
		cereal::BinaryOutputArchive archive(ss);
		archive(data);

		// compare the two results
		auto cereal_ret = ss.str();
		EXPECT_EQ(ret.size(), cereal_ret.size());
		EXPECT_TRUE(std::ranges::equal(ret, cereal_ret, [](auto e, auto byte) { return e == byte; }));

		return true;
	}

public:

	template <typename ... Ts>
	void primitive_test(std::tuple<Ts...>& data)
	{
		EXPECT_TRUE(TestHelper::call_helper(data, [this](auto& e) { primitive_helper(e); }));
	}

};

TYPED_TEST_SUITE_P(PrimitiveSuite);

std::tuple primitive_test_data{3, 3.0, 3.0f, 3.0L, 3l, 3ll, 3ul, 3ll, 0, 0.0, 0.0f, 0.0L, 0l, 0ll, 0ul, 0ll, -3, -3.0, -3.0f, -3.0L, -3l, -3ll, -3ul, -3ll, 'c'};

TYPED_TEST_P(PrimitiveSuite, PrimitiveTests)
{
	this->primitive_test(primitive_test_data);

	// FUZZY TESTS
	// fille primitive_test_data with random data
	for (int i = 0; i < 128; ++i)
	{
		[] <std::size_t ... idx> (std::index_sequence<idx...>)
		{
			([]() { std::get<idx>(primitive_test_data) = std::rand() / 100000.0; }, ...);
		}(std::index_sequence_for<decltype(primitive_test_data)>{});

		this->primitive_test(primitive_test_data);
	}

}

REGISTER_TYPED_TEST_SUITE_P(PrimitiveSuite, PrimitiveTests);
INSTANTIATE_TYPED_TEST_SUITE_P(PrimitiveTests, PrimitiveSuite, typename typelist_from_tuple<decltype(primitive_test_data)>::type);


template <typename T>
class PrimitiveRangeSuite : public TestHelper
{
	bool range_helper(std::ranges::range auto data)
	{
		auto ret = MILI::serialize(data);

		// serialize the array with cereal
		std::stringstream ss;
		cereal::BinaryOutputArchive archive(ss);

		for (const auto& e : data)
			archive(e);

		// compare the two results
		auto cereal_ret = ss.str();
		EXPECT_EQ(ret.size(), cereal_ret.size());
		EXPECT_TRUE(std::ranges::equal(ret, cereal_ret, [](auto e, auto byte) { return e == byte; }));

		return true;
	}

public:

	template <typename ... Ts>
	void range_test(std::tuple<Ts...>& data)
	{
		EXPECT_TRUE(call_helper(data, [this](auto& e) { range_helper(e); }));
	}

};

TYPED_TEST_SUITE_P(PrimitiveRangeSuite);


std::array<int, 6> range_data{ 1, 2, 3, 4, 5, 6 };
std::tuple range_test_data
{
	std::array<int, 3>{1, 2, 3},
	std::array<float, 3>{ 1.0f, 2.0f, 3.0f },
	std::array<double, 3>{ 1.0, 2.0, 3.0 },
	std::array<long double, 3>{ 1.0L, 2.0L, 3.0L },
	std::array<long, 3>{ 1l, 2l, 3l },
	std::array<long long, 3>{ 1ll, 2ll, 3ll },
	std::array<unsigned long, 3>{ 1ul, 2ul, 3ul },
	std::array<unsigned long long, 3> { 1ull, 2ull, 3ull },
	std::array<std::byte, 3>{ std::byte{1}, std::byte{2}, std::byte{3}},
	std::vector<int>{ 1, 2, 3 },
	std::vector<float>{ 1.0f, 2.0f, 3.0f },
	std::vector<double> { 1.0, 2.0, 3.0 },
	std::set<int>{ 1, 2, 3 },
	std::set<float>{ 1.0f, 2.0f, 3.0f },
	std::set<double>{ 1.0, 2.0, 3.0 },
	std::string { "abc" },
	range_data | ranges::views::stride(2),
};

TYPED_TEST_P(PrimitiveRangeSuite, RangeTests)
{
	this->range_test(range_test_data);

	// FUZZ Test
	// fill range_test_data with random data
	for (int i = 0; i < 128; ++i)
	{
		[&] <std::size_t ... idx> (std::index_sequence<idx...>)
		{
			([&]()
			{
				auto& r = std::get<idx>(range_test_data);

				for (auto& e : r)
					e = std::rand() / 100000.0;
			}, ...);
		}(std::index_sequence_for<decltype(range_test_data)>{});

		this->range_test(range_test_data);
	}
}

REGISTER_TYPED_TEST_SUITE_P(PrimitiveRangeSuite, RangeTests);
INSTANTIATE_TYPED_TEST_SUITE_P(PrimitiveRangeTests, PrimitiveRangeSuite, typename typelist_from_tuple<decltype(range_test_data)>::type);
