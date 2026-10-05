#include "WorldPause.h"

namespace WorldPause
{
	namespace
	{
		// AE 1.6 / 1.7 (found in 1.7.104): the console's ToggleAI routine (flips ProcessLists::runSchedules and stops the
		// actors' current movement when switching off) and the combat AI switch flipped by ToggleCombatAI (1 = on)
		constexpr std::uint64_t kToggleAI = 41327;
		constexpr std::uint64_t kCombatAI = 380233;

		bool active{ false };
		bool pausedAI{ false };
		bool pausedCombat{ false };
		bool pausedDetection{ false };
		bool madeGod{ false };

		void ToggleAI(RE::ProcessLists* a_lists)
		{
			using func_t = void (*)(RE::ProcessLists*);
			REL::Relocation<func_t> func{ REL::ID(kToggleAI) };
			func(a_lists);
		}

		bool* CombatAI()
		{
			static REL::Relocation<bool*> flag{ REL::ID(kCombatAI) };
			return flag.get();
		}

		// CommonLib's PlayerCharacter::SetGodMode passes the player as the first argument, but the game's function
		// is static (AE 40500 = mov [god mode flag], cl), so it stored the pointer's low byte and never turned god
		// mode off. The flag is the one IsGodMode reads.
		void SetGodMode(bool a_enable)
		{
			static REL::Relocation<bool*> flag{ RELOCATION_ID(517711, 404238) };
			*flag = a_enable;
		}
	}

	void Begin()
	{
		if (active) {
			return;
		}
		active = true;
		const auto lists = RE::ProcessLists::GetSingleton();
		if (lists) {
			if (lists->runSchedules) {
				if (REL::Module::IsAE()) {
					ToggleAI(lists);
				} else {
					lists->runSchedules = false;
				}
				pausedAI = !lists->runSchedules;
			}
			if (lists->runDetection) {
				lists->runDetection = false;
				pausedDetection = true;
			}
		}
		if (REL::Module::IsAE() && *CombatAI()) {
			*CombatAI() = false;
			pausedCombat = true;
		}
		if (!RE::PlayerCharacter::IsGodMode()) {
			SetGodMode(true);
			madeGod = true;
		}
		logs::info("World waits (AI {}, combat AI {}, detection {}, no damage {})", pausedAI, pausedCombat, pausedDetection, madeGod);
	}

	void End()
	{
		if (!active) {
			return;
		}
		active = false;
		const auto lists = RE::ProcessLists::GetSingleton();
		if (lists) {
			if (pausedAI && !lists->runSchedules) {
				if (REL::Module::IsAE()) {
					ToggleAI(lists);
				} else {
					lists->runSchedules = true;
				}
			}
			if (pausedDetection) {
				lists->runDetection = true;
			}
		}
		if (pausedCombat) {
			*CombatAI() = true;
		}
		if (madeGod && RE::PlayerCharacter::IsGodMode()) {
			SetGodMode(false);
		}
		pausedAI = pausedCombat = pausedDetection = madeGod = false;
		logs::info("World goes on");
	}

	void Reset()
	{
		End();
	}
}
