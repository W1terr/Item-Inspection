#include "Seen.h"

#include <mutex>
#include <unordered_set>

namespace Seen
{
	namespace
	{
		constexpr std::uint32_t kUniqueID = 'IINS';
		constexpr std::uint32_t kRecord = 'SEEN';
		constexpr std::uint32_t kVersion = 1;

		std::mutex                     lock;  // the menu reads the count on its own thread
		std::unordered_set<RE::FormID> items;

		void OnSave(SKSE::SerializationInterface* a_intfc)
		{
			std::scoped_lock guard(lock);
			if (!a_intfc->OpenRecord(kRecord, kVersion)) {
				logs::error("Couldn't write the seen items to the co-save");
				return;
			}
			const auto count = static_cast<std::uint32_t>(items.size());
			a_intfc->WriteRecordData(count);
			for (const auto id : items) {
				a_intfc->WriteRecordData(id);
			}
		}

		void OnLoad(SKSE::SerializationInterface* a_intfc)
		{
			std::scoped_lock guard(lock);
			items.clear();
			std::uint32_t type = 0, version = 0, length = 0;
			while (a_intfc->GetNextRecordInfo(type, version, length)) {
				if (type != kRecord || version != kVersion) {
					continue;
				}
				std::uint32_t count = 0;
				a_intfc->ReadRecordData(count);
				for (std::uint32_t i = 0; i < count; ++i) {
					RE::FormID id = 0;
					RE::FormID resolved = 0;
					if (a_intfc->ReadRecordData(id) && a_intfc->ResolveFormID(id, resolved)) {
						items.insert(resolved);  // forms of plugins that were removed are dropped
					}
				}
			}
			logs::info("Loaded {} seen items", items.size());
		}

		void OnRevert(SKSE::SerializationInterface*)
		{
			std::scoped_lock guard(lock);
			items.clear();
		}
	}

	void Register()
	{
		const auto serialization = SKSE::GetSerializationInterface();
		serialization->SetUniqueID(kUniqueID);
		serialization->SetSaveCallback(OnSave);
		serialization->SetLoadCallback(OnLoad);
		serialization->SetRevertCallback(OnRevert);
	}

	bool Contains(RE::FormID a_baseID)
	{
		std::scoped_lock guard(lock);
		return items.contains(a_baseID);
	}

	void Add(RE::FormID a_baseID)
	{
		std::scoped_lock guard(lock);
		items.insert(a_baseID);
	}

	std::size_t Count()
	{
		std::scoped_lock guard(lock);
		return items.size();
	}

	void Clear()
	{
		std::scoped_lock guard(lock);
		items.clear();
	}
}
