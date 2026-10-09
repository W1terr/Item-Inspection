#include "StoreDisplay.h"

namespace StoreDisplay
{
	namespace
	{
		constexpr std::string_view kPlugin = "Purchaseable Store-Display-Items.esp";
		constexpr RE::FormID       kBuyPerk = 0x590C;  // PSDI_BuyPerk: its activation entries and their script fragments
		constexpr const char*      kScript = "PRKF_PSDI_BuyPerk_0600590C";
		// PSDI 1.2.2: every store fragment (stores, general goods, city, castle, buy everywhere) does the same thing:
		// the item into the alias that names it in the message box, then BuyItem
		constexpr const char*      kBuyFragment = "Fragment_95";
		constexpr std::string_view kBuyLabel = "$PSDI_Buy";  // the activation label of its store entries ("$Consume" = taverns, homes)

		RE::BGSPerk* Perk()
		{
			static RE::BGSPerk* const perk = []() -> RE::BGSPerk* {
				const auto handler = RE::TESDataHandler::GetSingleton();
				const auto found = handler ? handler->LookupForm<RE::BGSPerk>(kBuyPerk, kPlugin) : nullptr;
				if (found) {
					logs::info("Purchaseable Store-Display-Items is loaded: items for sale go to the hand, buying happens when they go into the backpack");
				}
				return found;
			}();
			return perk;
		}

		// tells us when PSDI's script returned (its message box answered, the item bought or not)
		class Done : public RE::BSScript::IStackCallbackFunctor
		{
		public:
			explicit Done(std::function<void()> a_func) :
				func(std::move(a_func))
			{}

			void operator()(RE::BSScript::Variable) override
			{
				SKSE::GetTaskInterface()->AddTask(func);
			}

			void SetObject(const RE::BSTSmartPointer<RE::BSScript::Object>&) override {}

		private:
			std::function<void()> func;
		};
	}

	bool Installed()
	{
		return Perk() != nullptr;
	}

	bool ForSale(RE::TESObjectREFR* a_ref)
	{
		const auto perk = Perk();
		const auto player = RE::PlayerCharacter::GetSingleton();
		if (!perk || !a_ref || !player || !player->HasPerk(perk)) {
			return false;
		}
		// the activation label the crosshair would show: the highest priority label entry whose conditions hold
		// (perk owner tab = the player, target tab = the item)
		RE::TESForm*                 args[2]{ player, a_ref };
		RE::BGSEntryPointPerkEntry* shown = nullptr;
		for (const auto entry : perk->perkEntries) {
			if (!entry || entry->GetType() != RE::PERK_ENTRY_TYPE::kEntryPoint) {
				continue;
			}
			const auto point = static_cast<RE::BGSEntryPointPerkEntry*>(entry);
			const auto data = point->functionData;
			if (!point->IsEntryPoint(RE::BGSEntryPoint::ENTRY_POINT::kSetActivateLabel) || point->entryData.numArgs != 2 || !data ||
				data->GetType() != RE::BGSEntryPointFunctionData::ENTRY_POINT_FUNCTION_DATA::kText) {
				continue;
			}
			if ((shown && point->GetPriority() <= shown->GetPriority()) || !point->CheckConditionFilters(2, args)) {
				continue;
			}
			shown = point;
		}
		if (!shown) {
			return false;
		}
		const auto& label = static_cast<RE::BGSEntryPointFunctionDataText*>(shown->functionData)->text;
		return label.c_str() && kBuyLabel == label.c_str();
	}

	bool IsActivationEntry(const RE::BGSEntryPointPerkEntry* a_entry)
	{
		// the entry point first: only activating an item asks these entries (in game, the data is loaded by then)
		return a_entry->IsEntryPoint(RE::BGSEntryPoint::ENTRY_POINT::kActivate) && a_entry->perk && a_entry->perk == Perk();
	}

	bool Buy(RE::TESObjectREFR* a_ref, std::function<void()> a_done)
	{
		const auto perk = Perk();
		const auto player = RE::PlayerCharacter::GetSingleton();
		const auto vm = RE::BSScript::Internal::VirtualMachine::GetSingleton();
		if (!perk || !a_ref || !player || !vm) {
			return false;
		}
		const auto policy = vm->GetObjectHandlePolicy();
		const auto handle = policy ? policy->GetHandleForObject(RE::BGSPerk::FORMTYPE, perk) : 0;
		RE::BSTSmartPointer<RE::BSScript::Object> script;
		if (!handle || !vm->FindBoundObject(handle, kScript, script) || !script) {
			logs::warn("Purchaseable Store-Display-Items' script {} isn't running: can't buy {:08X}", kScript, a_ref->GetFormID());
			return false;
		}
		RE::BSTSmartPointer<RE::BSScript::IStackCallbackFunctor> callback{ new Done(std::move(a_done)) };
		const auto args = RE::MakeFunctionArguments(static_cast<RE::TESObjectREFR*>(a_ref), static_cast<RE::Actor*>(player));
		if (!vm->DispatchMethodCall(script, kBuyFragment, args, callback)) {
			logs::warn("Couldn't call {}.{} to buy {:08X}", kScript, kBuyFragment, a_ref->GetFormID());
			return false;
		}
		logs::info("Asking to buy {:08X} (Purchaseable Store-Display-Items)", a_ref->GetFormID());
		return true;
	}
}
