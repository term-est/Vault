#pragma once

#include <cstdint>
#include <optional>
#include <array>
#include <string_view>
#include <vector>
#include <map>
#include <set>
#include <span>
#include <ranges>
#include <dirent.h>
#include <unistd.h>

#include "nlohmann/json.hpp"
#include "range/v3/all.hpp"

#include "Serializer.hpp"

namespace MILI::Database
{
	static constexpr const char* const database_path = "/MILI/Vault/";

	using namespace std::literals;

	namespace details
	{

	template <typename Key, typename Value>
	struct DefaultSerializer
	{
		static auto serialize(Key key) noexcept
		{
			return MILI::serialize(key);
		}

		static auto serialize(Value value) noexcept
		{
			return MILI::serialize(value);
		}

		template <typename T>
		static auto deserialize(std::span<const std::byte> buffer) noexcept
		{
			return MILI::deserialize<T>(buffer);
		}
	};

	struct Header
	{
		std::array<char, 4> magic{'M', 'I', 'L', 'I'};
		std::uint32_t size{};
		std::uint16_t len{};

		Header() noexcept = default;

		bool construct(const std::array<std::byte, 16>& raw_data) noexcept
		{
			magic = MILI::deserialize<char, 4>(std::span<const std::byte, 4>{raw_data.begin(), raw_data.begin() + sizeof(magic)});

			// check if magic is correct
			if (magic != std::array<char, 4>{'M', 'I', 'L', 'I'})
				return false;

			size = MILI::deserialize<std::uint32_t>(std::span<const std::byte, sizeof(size)>{raw_data.begin() + sizeof(magic), raw_data.begin() + sizeof(magic) + sizeof(size)});
			len = MILI::deserialize<std::uint16_t>(std::span<const std::byte, sizeof(len)>{raw_data.begin() + sizeof(magic) + sizeof(size), raw_data.begin() + sizeof(magic) + sizeof(size) + sizeof(len)});

			return true;
		}

		[[nodiscard]]
		std::array<std::byte, 16> serialize() const noexcept
		{
			using namespace MILI::Database;

			auto&& serialized_vec = MILI::serialize(magic, size, len, std::array<std::byte, 6>{});
			std::array<std::byte, 16> serialized_array{};
			std::copy(serialized_vec.begin(), serialized_vec.end(), serialized_array.begin());

			return serialized_array;
		}
	};

	template <typename Key, typename Value, typename Serializer, std::size_t BucketSize = 128>
	class Engine;

// todo: use static memory
	template <typename Key, typename Value, typename Serializer, typename Container = std::map<Key, Value>>
	class Bucket
	{
	public:

//		Bucket(const Bucket&) = delete;
//
//		Bucket(Bucket&& rhs) noexcept : data{std::move(rhs.data)}
//		{
//			rhs.is_moved = true;
//		}

		bool flush() noexcept
		{
			needs_flusing = false;
			FILE* file = fopen((database_path + db_name + "/" + table_name + "/fragment" + std::to_string(id)).c_str(), "wb");

			if (!file)
				return false;

			Header header;
			header.len = static_cast<std::uint16_t>(data.size());
			fwrite(header.serialize().data(), 16, 1, file);

			// TODO: fix serialization


			for (const auto& e : data)
			{
				auto serialized_key = Serializer::serialize(e.first);
				auto serialized_key_size = MILI::serialize<std::uint16_t>(serialized_key.size());

				fwrite(serialized_key_size.data(), sizeof(serialized_key_size), 1, file);
				fwrite(serialized_key.data(), serialized_key.size(), 1, file);
				header.size += serialized_key_size.size() + sizeof(serialized_key_size);

				auto serialized_value = Serializer::serialize(e.second);
				auto serialized_val_size = MILI::serialize<std::uint16_t>(serialized_value.size());

				fwrite(serialized_val_size.data(), sizeof(serialized_val_size), 1, file);
				fwrite(serialized_value.data(), serialized_value.size(), 1, file);
				header.size += serialized_val_size.size() + serialized_value.size() + serialized_key_size.size() + serialized_key_size.size();
			}

			// rewrite the header
			fseek(file, 0, SEEK_SET);
			fwrite(header.serialize().data(), 16, 1, file);

			fclose(file);

			return true;
		}

		bool update(const Key& key, Value value)
		{
			auto itr = data.find(key);

			if (itr == data.end())
				return false;

			needs_flusing = true;
			itr->second = value;

			return true;
		}

		bool remove(const Key& key) noexcept
		{
			auto itr = data.find(key);

			if (itr == data.end())
				return false;

			needs_flusing = true;
			data.erase(itr);

			return true;
		}

		bool insert(Key key, Value value) noexcept
		{
			auto itr = data.find(key);

			if (itr != data.end())
				return false;

			needs_flusing = true;
			data[key] = value;

			return true;
		}

		std::optional<Value> read(const Key& key) noexcept
		{
			auto itr = data.find(key);

			if (itr == data.end())
				return std::nullopt;

			return itr->second;
		}

		[[nodiscard]]
		std::size_t get_id() const noexcept
		{
			return id;
		}

		[[nodiscard]]
		std::string_view get_name() const noexcept
		{
			return table_name;
		}


		~Bucket() noexcept
		{
			if (needs_flusing)
				flush();
		}

	private:

		template <typename K, typename V, typename Serializer_, std::size_t BucketSize>
		friend class Engine;

		explicit Bucket(std::string_view db, std::string_view tbl_name, std::size_t bucket_idx) noexcept : db_name{db}, table_name{tbl_name}, id{bucket_idx}
		{
			Header header{};
			FILE* file = fopen((database_path + db_name + "/" + table_name + "/fragment" + std::to_string(id)).c_str(), "rb");

			if (not file)
				return;

			std::array<std::byte, 16> raw_header{};

			if (fread(raw_header.data(), sizeof(std::byte), raw_header.size(), file) != raw_header.size())
			{
				fclose(file);
				return;
			}

			if (not header.construct(raw_header))
				return;


			auto get_size = [&]() -> std::uint16_t
			{
				std::array<std::byte, 2> data_info{};

				if (fread(data_info.data(), sizeof(std::byte), data_info.size(), file) != data_info.size())
					return 0;

				return MILI::deserialize<std::uint16_t>(data_info);
			};


			std::vector<std::byte> buffer;

			while (std::uint16_t size = get_size())
			{
				buffer.resize(size);
				if (fread(buffer.data(), sizeof(std::byte), size, file) != size)
					break;

				Key key = Serializer::template deserialize<Key>(std::span<const std::byte>(buffer.data(), buffer.size()));
				buffer.clear();

				size = get_size();
				buffer.resize(size);
				if (fread(buffer.data(), sizeof(std::byte), size, file) != size)
					break;

				Value val = Serializer::template deserialize<Value>(std::span<const std::byte>(buffer.data(), buffer.size()));

				data[key] = val;
				buffer.clear();
			}

			fclose(file);
		}

		Container data;
		std::string db_name;
		std::string table_name;
		std::size_t id = 0;
		bool is_moved = false;
		bool needs_flusing = false;
	};

	template <typename Key, typename Value, typename Serializer, std::size_t BucketSize>
	class Engine
	{
		std::string_view db_name;
		std::set<std::string_view> data;
	public:

		constexpr static std::size_t bucket_size = BucketSize;

		explicit Engine(std::string_view name) noexcept : db_name{name}
		{}

		bool integrity_check() noexcept
		{
			// check if a folder named /MILI/Vault/ exists
			DIR* dir = opendir(database_path);

			if (not dir)
				return false;

			closedir(dir);

			// TODO: stricter integrity check
			// regenerate hash file if it's corrupted?

			return true;
		}

		void construct() noexcept
		{
			for (auto dir : {"/MILI", "/MILI/Vault"})
				mkdir(dir, 0777);
		}

		auto get_bucket(std::string_view table_name, std::size_t bucket_number) noexcept -> std::optional<details::Bucket<Key, Value, Serializer>>
		{
			// convert read file named table_name from the disk under the MILI/Vault/ directory
			std::string&& path = database_path + std::string{db_name} + "/" + std::string{table_name};
			std::string&& filename = path + "/fragment" + std::to_string(bucket_number);
			// check if file exists
			FILE* file = fopen(filename.c_str(), "r");

			if (not file)
			{
				using namespace std::literals;
				for (const auto& dir : {"/MILI"s, "/MILI/Vault"s, (database_path + std::string{db_name}), path})
					mkdir(dir.c_str(), 0777);

				Header header{};

				// create file
				file = fopen(filename.c_str(), "w");

				if (not file)
					return std::nullopt;

				// write header
				auto&& serialized_header = header.serialize();
				fwrite(serialized_header.data(), sizeof(std::byte), 16, file);
				fclose(file);

				return Bucket<Key, Value, Serializer>{db_name, table_name, bucket_number};
			}

			// read	header
			std::array<std::byte, 16> header_data{};
			fread(header_data.data(), sizeof(std::byte), 16, file);
			Header header{};
			header.construct(header_data);
			fclose(file);

			return Bucket<Key, Value, Serializer>{db_name, table_name, bucket_number};
		}
	};

	}

template <typename Key, typename Value>
struct Cache
{
	enum class Operation
	{
		Insert,
		Remove,
		Update
	};

	struct Entry
	{
		std::string table;
		Key key;
		Value value;
		Operation operation;
	};

	std::vector<Entry> entries;
};



template <typename Key, typename Value, typename Serializer = details::DefaultSerializer<Key, Value>>
class Vault
{
	constexpr static std::size_t cache_size = 16;
	using Engine = details::Engine<Key, Value, Serializer, 64>;
	using bucket_t = decltype(std::declval<Engine>().get_bucket("table_name", 0));

	Engine engine{};
	std::string name;
	bucket_t bucket{std::nullopt};
	Cache<Key, Value> cache;
	std::set<std::size_t> hash_map;

	explicit Vault(Engine eng, std::string_view db_name = "Vault") noexcept : engine{eng}, name{db_name}
	{
		// initialize the hash map
		FILE* hash_file = fopen((database_path + name + ".hash").c_str()  , "rb");

		// read the file if it exists
		if (hash_file)
		{
			std::array<std::byte, sizeof(std::size_t)> data{};
			fread(data.data(), sizeof(std::byte), data.size(), hash_file);

			auto&& size = MILI::deserialize<std::size_t>(data);

			while (fread(data.data(), sizeof(std::byte), data.size(), hash_file) == data.size())
			{
				const auto hash = MILI::deserialize<std::size_t>(data);
				hash_map.insert(hash);
			}
		}


		fclose(hash_file);
	}

	class Table
	{
		friend class Vault;

		std::string name;
		Vault& vault;

		explicit Table(std::string_view table_name, Vault& v) noexcept : name{table_name}, vault{v}
		{}

	public:

		[[nodiscard]]
		std::optional<Value> read(const Key& key) noexcept
		{
			const std::size_t hash = std::hash<Key>{}(key);

			// if hash does not exist in the hash map, return std::nullopt
			if (not vault.hash_map.count(hash))
				return std::nullopt;

			// search the cache for the key
			for (const auto& entry : vault.cache.entries)
			{
				if (entry.key == key)
				{
					if (entry.operation == Cache<Key, Value>::Operation::Remove)
						return std::nullopt;

					else
						return entry.value;
				}
			}

			// check if we have the correct bucket
			if (not vault.bucket or vault.bucket->get_name() != name or vault.bucket->get_id() != (hash % vault.engine.bucket_size))
			{
				vault.bucket = std::nullopt;
				vault.bucket = vault.engine.get_bucket(name, hash % vault.engine.bucket_size);
			}

			auto ret = vault.bucket->read(key);

			return ret;
		}


		bool update(const Key& key, Value value)
		{
			const std::size_t hash = std::hash<Key>{}(key);

			// if hash does not exist in the hash map, return false
			if (not vault.hash_map.count(hash))
				return false;

			// search the cache for the key
			for (auto& entry : vault.cache.entries)
			{
				if (entry.key == key)
				{
					if (entry.operation == Cache<Key, Value>::Operation::Remove)
						return false;

					entry.value = value;
					entry.operation = Cache<Key, Value>::Operation::Update;

					// add the hash to the hash map
					vault.hash_map.insert(hash);
					return true;
				}
			}

			// add the entry to the cache
			vault.cache.entries.push_back(typename Cache<Key, Value>::Entry{name, key, value, Cache<Key, Value>::Operation::Update});

			// add the hash to the hash map
			vault.hash_map.insert(hash);

			if (vault.cache.entries.size() > cache_size)
				vault.flush();

			return true;
		}

		[[nodiscard]]
		bool insert(const Key& key, Value value) noexcept
		{
			const std::size_t hash = std::hash<Key>{}(key);

			// search the cache to make sure that we don't have in it
			for (auto& entry : vault.cache.entries)
			{
				if (entry.key == key)
				{
					if (entry.operation != Cache<Key, Value>::Operation::Remove)
						return false;

					entry.value = value;
					entry.operation = Cache<Key, Value>::Operation::Update;

					// add the hash to the hash map
					vault.hash_map.insert(hash);
					return true;
				}
			}

			// check if we have the correct bucket
			if (not vault.bucket or vault.bucket->get_name() != name or vault.bucket->get_id() != hash % vault.engine.bucket_size)
			{
				vault.bucket = std::nullopt;
				vault.bucket = vault.engine.get_bucket(name, hash % vault.engine.bucket_size);
			}

			// if the bucket already has the entry, return false
			if (vault.bucket->read(key))
				return false;

			// add data to the cache and the hash map
			vault.cache.entries.push_back(typename Cache<Key, Value>::Entry{name, key, value, Cache<Key, Value>::Operation::Insert});
			vault.hash_map.insert(hash);

			// if the cache is full, flush it
			if (vault.cache.entries.size() >= cache_size)
				vault.flush();


			return true;
		}

		[[nodiscard]]
		bool remove(const Key& key) noexcept
		{
			const std::size_t hash = std::hash<Key>{}(key);

			// if hash does not exist in the hash map, return false
			if (not vault.hash_map.count(hash))
				return false;

			// check the cache
			for (auto& entry : vault.cache.entries)
			{
				if (entry.key == key)
				{
					if (entry.operation == Cache<Key, Value>::Operation::Remove)
						return false;

					entry.operation = Cache<Key, Value>::Operation::Remove;

					// remove the hash from the map
					vault.hash_map.erase(hash);
					return true;
				}
			}

			// add operation to the cache to be performed later
			vault.cache.entries.push_back(typename Cache<Key, Value>::Entry{name, key, Value{}, Cache<Key, Value>::Operation::Remove});
			vault.hash_map.erase(hash);

			// if the cache is full, flush it
			if (vault.cache.entries.size() >= cache_size)
				vault.flush();

			return true;
		}

	};

public:


	// The goal is to not allow more than one instance for a database
	static auto get_instance(std::string_view db_name) noexcept -> std::optional<std::reference_wrapper<Vault>>
	{
		Engine db_engine{db_name};

		if (not db_engine.integrity_check())
			return std::nullopt;

		else
		{
			static Vault vault{db_engine, db_name}; // TODO:: lifetime management?
			return std::optional{std::ref(vault)};
		}
	}

	static auto construct(std::string_view db_name) noexcept -> std::optional<std::reference_wrapper<Vault>>
	{
		Engine db_engine{db_name};

		if (db_engine.integrity_check())
			return std::nullopt;

		db_engine.construct();
		return get_instance(db_name);

	}

	Table table(std::string_view table_name) noexcept
	{
		return Table{table_name, *this};
	}

	bool flush() noexcept
	{
		// sort the cache by bucket number
		std::sort(cache.entries.begin(), cache.entries.end(), [&](const auto& lhs, const auto& rhs) {
			return (std::hash<Key>{}(lhs.key) % engine.bucket_size) < (std::hash<Key>{}(rhs.key) % engine.bucket_size);
		});

		for (const auto& entry: cache.entries)
		{
			// get the bucket
			const auto bucket_number = std::hash<Key>{}(entry.key) % engine.bucket_size;

			if (not bucket or bucket->get_name() != entry.table or bucket->get_id() != bucket_number)
			{
				bucket = std::nullopt; // flush the damn thing
				bucket = engine.get_bucket(entry.table, bucket_number);
			}

			if (bucket == std::nullopt)
				return false;

			switch (entry.operation)
			{
				case Cache<Key, Value>::Operation::Insert:

					if (not bucket.value().insert(entry.key, entry.value))
						assert(bucket.value().update(entry.key, entry.value));

				break;

				case Cache<Key, Value>::Operation::Update:

					if (not bucket.value().update(entry.key, entry.value))
						assert(bucket.value().insert(entry.key, entry.value));

				break;

				case Cache<Key, Value>::Operation::Remove: bucket.value().remove(entry.key);
					break;

				default:
					return false;
			}
		}

		cache.entries.clear();

		auto&& hash_data = MILI::serialize(hash_map);
		auto&& hash_size = MILI::serialize(hash_map.size());

		FILE* hash_file = fopen((database_path + name + ".hash").c_str(), "wb");

		if (not hash_file)
			return false;

		fwrite(hash_size.data(), hash_size.size(), 1, hash_file);
		fwrite(hash_data.data(), hash_data.size(), 1, hash_file);

		fclose(hash_file);

		bucket = std::nullopt;

		return false;
	}

	~Vault() noexcept
	{
		flush();

		// TODO: Use MILI::is_a concept
		// TODO: this should be done as part of the flush operation
		// serialize the hash map and write it to a file
		auto&& hash_data = MILI::serialize(hash_map);
		auto&& hash_size = MILI::serialize(hash_map.size());
	}

};

}
