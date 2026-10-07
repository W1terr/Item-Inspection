#include "ContainerLid.h"

#include <atomic>
#include <cstring>

namespace ContainerLid
{
	namespace
	{
		using OpenState = RE::BGSOpenCloseForm::OPEN_STATE;
		using SetOpenState_t = bool (*)(RE::TESObjectREFR*, bool, bool);

		std::atomic<RE::TESObjectREFR*> kept{ nullptr };  // only compared, never used: no reference held
		SetOpenState_t                  original{ nullptr };
		bool                            installed{ false };
		RE::ObjectRefHandle             closeWhenOpen;  // released while the lid was still opening
		float                           closeWait{ 0.0f };
		constexpr float                 kMaxCloseWait = 3.0f;

		// BGSOpenCloseForm::SetOpenState (SE 14179 / AE 14287) starts with push rbx / push rsi / push r12 (1.7.104):
		// whole instructions without addresses in them, so they can run from a copy before jumping back behind them
		constexpr std::array<std::uint8_t, 5> kPrologue{ 0x40, 0x53, 0x56, 0x41, 0x54 };

		// called by the game, scripts and QuickLoot (also from a task its thread queues) to open or close a reference
		bool SetOpenState(RE::TESObjectREFR* a_ref, bool a_open, bool a_snap)
		{
			if (!a_open && a_ref && a_ref == kept.load()) {
				return false;  // the item from it is still in the hand
			}
			return original(a_ref, a_open, a_snap);
		}

		bool Opened(RE::TESObjectREFR* a_ref)
		{
			const auto state = RE::BGSOpenCloseForm::GetOpenState(a_ref);
			return state == OpenState::kOpen || state == OpenState::kOpening;
		}
	}

	void Install()
	{
		const auto address = RELOCATION_ID(14179, 14287).address();
		if (std::memcmp(reinterpret_cast<const void*>(address), kPrologue.data(), kPrologue.size()) != 0) {
			logs::warn("BGSOpenCloseForm::SetOpenState not as expected: containers may close while their item is held");
			return;
		}
		auto& trampoline = SKSE::GetTrampoline();
		// the original: its first instructions, then jmp [rip+0] to the rest of it
		const auto stub = static_cast<std::uint8_t*>(trampoline.allocate(kPrologue.size() + 14));
		std::memcpy(stub, kPrologue.data(), kPrologue.size());
		const auto jump = stub + kPrologue.size();
		jump[0] = 0xFF;
		jump[1] = 0x25;
		std::memset(jump + 2, 0, 4);
		const std::uintptr_t rest = address + kPrologue.size();
		std::memcpy(jump + 6, &rest, sizeof(rest));
		original = reinterpret_cast<SetOpenState_t>(stub);
		trampoline.write_branch<5>(address, SetOpenState);
		installed = true;
		logs::info("Hooked BGSOpenCloseForm::SetOpenState: a container stays open while its item is held");
	}

	void KeepOpen(RE::TESObjectREFR* a_container)
	{
		if (!installed || !a_container) {
			return;
		}
		kept = a_container;
		if (!Opened(a_container)) {
			RE::BGSOpenCloseForm::SetOpenState(a_container, true, false);
		}
	}

	void Release(RE::ObjectRefHandle a_container)
	{
		const auto container = a_container.get();
		if (!container || kept.load() != container.get()) {
			return;
		}
		kept = nullptr;
		const auto state = RE::BGSOpenCloseForm::GetOpenState(container.get());
		if (state == OpenState::kOpen) {
			RE::BGSOpenCloseForm::SetOpenState(container.get(), false, false);
		} else if (state == OpenState::kOpening) {
			closeWhenOpen = a_container;  // closing in the middle of opening isn't done (QuickLoot waits too)
			closeWait = 0.0f;
		}
	}

	void Update(float a_delta)
	{
		const auto container = closeWhenOpen.get();
		if (!container) {
			return;
		}
		closeWait += a_delta;
		const auto state = RE::BGSOpenCloseForm::GetOpenState(container.get());
		if (state == OpenState::kOpen && kept.load() != container.get()) {
			RE::BGSOpenCloseForm::SetOpenState(container.get(), false, false);
		} else if (state == OpenState::kOpening && closeWait < kMaxCloseWait) {
			return;
		}
		closeWhenOpen.reset();
	}

	void Forget()
	{
		kept = nullptr;
		closeWhenOpen.reset();
	}
}
