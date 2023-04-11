#include <iostream>
#include <queue>
#include <algorithm>
#include <ranges>
#include <vector>
#include <string_view>
#include <memory_resource>

#include "Vault.hpp"

#include "nlohmann/json.hpp"

using namespace std::literals;

struct Person
{
	std::string name = "some name";
};


std::vector<std::byte> serialize(Person& p)
{
	std::vector<std::byte> buffer;

	auto&& serialized_size = MILI::serialize(p.name.size());
	auto&& serialized_name = MILI::serialize(p.name);
	auto&& serialized_data = ranges::views::concat(serialized_size, serialized_name);
	buffer.insert(buffer.end(), serialized_data.begin(), serialized_data.end());

	return buffer;
}

Person deserialize(const std::vector<std::byte>& buffer)
{
	std::span<const std::byte, sizeof(std::size_t)> size_span{buffer.data(), sizeof(std::size_t)};
	const auto size = MILI::deserialize<std::size_t>(size_span);

	std::span<const char> name_span{reinterpret_cast<const char*>(buffer.data()) + sizeof(std::size_t), size};
	std::string name{name_span.begin(), name_span.end()};

	return Person{name};
}

// TODO: accept all buffer types?
// TODO: Add support for custom serializer passed as a serializer object
// TODO: Chunked reader


// this must be created with an optional thingy
template <std::size_t Size>
struct ChunkedContainer
{
	std::string_view file_name;

	ChunkedContainer(std::string_view file) : file_name{file}
	{}

	// make iterators
};

auto main() -> int
{
	MILI::Database::Vault<int, double>::construct("vault.db");
	auto vault = MILI::Database::Vault<int, double>::get_instance("vault.db");

	for (int i = 0; i < 32; ++i)
	{
		std::cout << vault->get().table("test2").read(i).value() << std::endl;
	}
}