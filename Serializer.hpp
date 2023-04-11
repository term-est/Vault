#include <concepts>
#include <array>
#include <span>

#include "range/v3/all.hpp"


namespace MILI
{

template <typename T>
concept Primitive = requires (std::remove_cvref_t<T> t)
{
	requires std::integral<decltype(t)> or std::floating_point<decltype(t)> or std::same_as<decltype(t), std::byte>;
};

template <Primitive T>
[[nodiscard]]
constexpr auto serialize(T&& data) noexcept -> std::array<std::byte, sizeof(T)>
{
	constexpr std::size_t Size = sizeof(T);
	std::array<std::byte, Size> buffer;

	auto&& begin = reinterpret_cast<const std::byte*>(&data);
	std::span<const std::byte, Size> view{begin, Size};

	std::copy(view.begin(), view.end(), buffer.begin());

	return buffer;
}


[[nodiscard]]
constexpr auto serialize(const std::ranges::range auto& data) noexcept -> std::vector<std::byte>
requires Primitive<std::ranges::range_value_t<decltype(data)>>
{
	std::vector<std::byte> buffer;

	std::size_t offset = 0;
	for (const auto& e : data)
	{
		auto&& serialized_data = serialize(e);
		buffer.insert(buffer.end(), serialized_data.begin(), serialized_data.end());
		offset += sizeof(serialized_data);
	}

	return buffer;
}

template <Primitive T, std::size_t Size>
[[nodiscard]]
constexpr auto serialize(std::span<T, Size> data) noexcept -> std::array<std::byte, sizeof(T) * Size>
{
	std::array<std::byte, sizeof(T) * Size> buffer;

	std::size_t offset = 0;
	for (const auto& e : data)
	{
		auto&& serialized_data = serialize(e);
		std::copy(serialized_data.begin(), serialized_data.end(), buffer.begin() + offset);
		offset += sizeof(serialized_data);
	}

	return buffer;
}

template <typename ... Ts>
[[nodiscard]]
constexpr auto serialize(Ts&& ... data) noexcept
requires (sizeof ... (Ts) > 1)
{
	std::vector<std::byte> ret;

	std::tuple data_tuple{std::forward<Ts>(data)...};

	[&]<std::size_t ... idx> (std::index_sequence<idx...>)
	{
		auto serialize_helper = [&] <std::size_t index>()
		{
			auto&& serialized_chunk = serialize(std::get<index>(data_tuple));
			ret.insert(ret.end(), serialized_chunk.begin(), serialized_chunk.end());
		};
		(serialize_helper. template operator()<idx>(), ...);

	}(std::index_sequence_for<Ts...>{});

	return ret;
}

template <typename T>
[[nodiscard]]
constexpr T deserialize(const std::ranges::range auto& buffer) noexcept
{
	T t;
	auto t_begin = reinterpret_cast<std::byte*>(&t);
	std::ranges::range auto t_range = std::ranges::subrange(t_begin, t_begin + sizeof(T));

	std::copy(buffer.begin(), buffer.end(), t_range.begin());

	return t;
}

template <typename T, std::size_t Size>
[[nodiscard]]
constexpr std::array<T, Size> deserialize(std::span<const std::byte, sizeof(T) * Size> buffer) noexcept
{
	std::array<T, Size> ret;

	auto beg = buffer.begin();

	for (int i = 0; i < Size; ++i)
	{
		auto& e = ret[i];
		e = deserialize<T>(std::span<const std::byte, sizeof(T)>{beg, beg + sizeof(T)});
		std::advance(beg, sizeof(T));
	}

	return ret;
}

template <std::integral T>
[[nodiscard]]
constexpr T deserialize(const std::array<std::byte, sizeof(T)>& buffer) noexcept
{
	return deserialize<T>(std::span<const std::byte, sizeof(T)>{buffer.begin(), buffer.end()});
}

template <std::integral T, std::size_t Size>
[[nodiscard]]
constexpr std::array<T, Size> deserialize(const std::array<std::byte, sizeof(T) * Size>& buffer) noexcept
{
	return deserialize<T, Size>(std::span<const std::byte, sizeof(T) * Size>{buffer.begin(), buffer.end()});
}


};
