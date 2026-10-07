#include "QuickLoot.h"

#include "Inspect.h"
#include "Settings.h"

#include <Windows.h>

#include <atomic>

// third party header (QuickLoot IE's modder API, MIT): keep its warnings out of our build
#pragma warning(push, 0)
#include "QuickLootAPI.h"
#pragma warning(pop)

namespace QuickLoot
{
	namespace
	{
		using Interface = API::QuickLootAPI;
		using Action = API::QuickLootAction;

		bool connected{ false };
		bool paused{ false };

		// QuickLoot sends the key's action first, then a "taking" event per stack: only a single "Take" goes to the
		// hand ("Take All" and "Use" / equip / read work as usual)
		std::atomic<Action> lastAction{ Action::kNone };

		// the game's check whether the owner lets the player take something worth this much (the same QuickLoot uses)
		bool AllowedToTake(RE::PlayerCharacter* a_player, RE::TESForm* a_owner, std::int32_t a_value)
		{
			using func_t = bool (*)(RE::PlayerCharacter*, RE::TESForm*, std::int32_t);
			static REL::Relocation<func_t> func{ RELOCATION_ID(39584, 40670) };
			return func(a_player, a_owner, a_value);
		}

		// the container's extra data list the game would remove for this many of the stack (QuickLoot's choice too)
		RE::ExtraDataList* ExtraListForRemoval(RE::InventoryEntryData* a_entry, std::int32_t a_count)
		{
			using func_t = RE::ExtraDataList* (*)(RE::InventoryEntryData*, std::int32_t, bool);
			static REL::Relocation<func_t> func{ RELOCATION_ID(50948, 51825) };
			return func(a_entry, a_count, true);
		}

		// whether taking this stack is stealing, decided the way QuickLoot decides it (it shows those in red)
		bool Stealing(RE::TESObjectREFR* a_container, RE::InventoryEntryData* a_entry)
		{
			const auto player = RE::PlayerCharacter::GetSingleton();
			const auto actor = a_container->As<RE::Actor>();
			const auto owner = a_container->GetOwner();
			if (a_container == player || (actor && actor->IsDead(false)) || !owner) {
				return false;  // the own inventory, the dead and unowned containers can't be stolen from
			}
			auto itemOwner = a_entry->GetOwner();
			if (!itemOwner) {
				itemOwner = actor ? static_cast<RE::TESForm*>(actor) : owner;
			}
			return !AllowedToTake(player, itemOwner, a_entry->GetValue());
		}

		void OnOpenLootMenu(API::OpenLootMenuEvent* a_event)
		{
			Inspect::ContainerShown(a_event->container);
		}

		void OnInputAction(API::InputActionEvent* a_event)
		{
			lastAction = a_event->action;
		}

		void OnTakingItem(API::TakingItemEvent* a_event)
		{
			const auto action = lastAction.exchange(Action::kNone);  // "Take All" sends one event per stack: only the first could match
			if (action != Action::kTake || a_event->result != API::HandleResult::kContinue || !Settings::Get().quickLoot ||
				a_event->actor != RE::PlayerCharacter::GetSingleton()) {
				return;
			}
			const auto stack = a_event->stack;
			const auto entry = stack ? stack->entry : nullptr;
			if (!entry || !entry->object || entry->countDelta <= 0 || stack->dropRef) {
				return;  // something the NPC dropped on the floor: QuickLoot picks it up, which our world pickup handles
			}
			const auto container = a_event->container.get();
			if (!container) {
				return;
			}
			const Inspect::ContainerItem item{
				.container = a_event->container,
				.object = entry->object,
				.count = entry->countDelta,
				.extraList = ExtraListForRemoval(entry, entry->countDelta),
				.stealing = Stealing(container.get(), entry),
				.value = entry->GetValue()
			};
			if (Inspect::OfferContainerItem(item)) {
				a_event->result = API::HandleResult::kStop;
			}
		}
	}

	void Connect()
	{
		if (!Interface::Init("ItemInspection", API::ApiVersion::kV21)) {
			logs::info("QuickLoot IE not found (or older than its API 2.1): taking items from containers works as usual");
			return;
		}
		Interface::RegisterInputActionHandler(OnInputAction);
		Interface::RegisterTakingItemHandler(OnTakingItem);
		Interface::RegisterOpenLootMenuHandler(OnOpenLootMenu);
		connected = true;
		logs::info("QuickLoot IE found: items it takes from containers can go to the hand ([QuickLoot] bQuickLoot)");
	}

	bool Installed()
	{
		return connected;
	}

	void Pause(bool a_pause)
	{
		if (!connected || a_pause == paused) {
			return;
		}
		paused = a_pause;
		if (a_pause) {
			Interface::DisableLootMenu();
		} else {
			Interface::EnableLootMenu();
			Interface::RefreshLootMenu();  // the container may have one item less now
		}
	}
}
