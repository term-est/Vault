#include <iostream>
#include <queue>
#include <algorithm>
#include <ranges>
#include <vector>
#include <string_view>
#include <memory_resource>

#include "Vault.hpp"

#include "nlohmann/json.hpp"
#include "mongoose.h"
#include "Server.hpp"

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

template <typename Key, typename Value>
struct Operation
{
	std::string operation;
	std::string table;
	Key key;
	Value value;


	nlohmann::json to_json() const
	{
		return nlohmann::json{{"operation", operation}, {"table", table}, {"key", key}, {"value", value}};
	}

	void from_json(const nlohmann::json& json)
	{
		operation = json["operation"];
		table = json["table"];
		key = json["key"];
		value = json["value"];
	}
};


auto main() -> int
{
	MILI::Database::Vault<int, double>::construct("vault.db");
	auto vault_opt = MILI::Database::Vault<int, double>::get_instance("vault.db");

	auto& vault = vault_opt.value().get();

	Server server{"http://0.0.0.0:8080"};

	auto cb = [&](mg_connection* c, int ev, void* ev_data)
	{
		if (ev == MG_EV_HTTP_MSG)
		{
			mg_http_message* hm = (mg_http_message*) ev_data;

			if (mg_http_match_uri(hm, "/ws"))
			{
				mg_ws_upgrade(c, hm, nullptr);
				c->data[0] = 'W';
			}
		}

		else if (ev == MG_EV_WS_MSG)
		{
			mg_ws_message* msg = (mg_ws_message*) ev_data;
			std::string_view msg_sv = {reinterpret_cast<const char*>(msg->data.ptr), msg->data.len};
			nlohmann::json json = nlohmann::json::parse(msg_sv);

			if (json.is_discarded())
			{
				nlohmann::json invalid_json{{"error", "Invalid JSON"}};
				std::string&& response = invalid_json.dump();
				mg_ws_send(c, response.data(), response.size(), WEBSOCKET_OP_TEXT);
			}

			else
			{
				Operation<int, double> operation;
				operation.from_json(json);

				nlohmann::json response{{"operation", operation.operation}, {"table", operation.table}, {"result", false}};

				if (operation.operation == "insert")
				{
					response["result"] = vault.table(operation.table).insert(operation.key, operation.value);
				}

				else if (operation.operation == "update")
				{
					response["result"] = vault.table(operation.table).update(operation.key, operation.value);
				}

				else if (operation.operation == "remove")
				{
					response["result"] = vault.table(operation.table).remove(operation.key);
				}

				else if (operation.operation == "read")
				{
					auto value = vault.table(operation.table).read(operation.key);

					if (value.has_value())
					{
						response["result"] = true;
						response["value"] = value.value();
					}
				}

				std::string&& resp_str = response.dump();
				mg_ws_send(c, resp_str.data(), resp_str.size(), WEBSOCKET_OP_TEXT);
			}

		}

	};

	server.listen(cb);

	while(true)
	{
		server.poll_events(std::chrono::milliseconds(100));

		// flush the database every 5 seconds
		if (std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count() % 5 == 0)
		{
			vault.flush();
		}
	}

}

