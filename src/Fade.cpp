#include "Fade.h"

namespace Fade
{
	namespace
	{
		constexpr RE::FormID kHoldBlack = 0x0F756E;  // Skyrim.esm IMAD "FadeToBlackHoldImod": full black for 3 s
		constexpr float      kRetrigger = 2.5f;      // seconds: start a new instance before the 3 s run out

		RE::TESImageSpaceModifier*                       imod{ nullptr };
		bool                                             looked{ false };
		RE::NiPointer<RE::ImageSpaceModifierInstanceForm> instance;
		float                                            strength{ 0.0f };
		float                                            target{ 0.0f };
		float                                            speed{ 0.0f };  // strength per second
		float                                            age{ 0.0f };

		void Lookup()
		{
			if (!looked) {
				looked = true;
				imod = RE::TESDataHandler::GetSingleton()->LookupForm<RE::TESImageSpaceModifier>(kHoldBlack, "Skyrim.esm");
				if (!imod) {
					logs::warn("FadeToBlackHoldImod not found, no fades");
				}
			}
		}

		void StopInstance()
		{
			if (instance && imod) {
				RE::ImageSpaceModifierInstanceForm::Stop(imod);
			}
			instance.reset();
		}
	}

	void To(float a_black, float a_seconds)
	{
		Lookup();
		target = std::clamp(a_black, 0.0f, 1.0f);
		speed = a_seconds > 0.0f ? 1.0f / a_seconds : 1000.0f;
	}

	void Update(float a_delta)
	{
		if (!imod || (strength == 0.0f && target == 0.0f && !instance)) {
			return;
		}
		if (strength < target) {
			strength = std::min(target, strength + speed * a_delta);
		} else if (strength > target) {
			strength = std::max(target, strength - speed * a_delta);
		}
		if (strength <= 0.0f) {
			StopInstance();
			return;
		}
		age += a_delta;
		if (!instance || age > kRetrigger) {
			StopInstance();
			instance.reset(RE::ImageSpaceModifierInstanceForm::Trigger(imod, strength, nullptr));
			age = 0.0f;
		}
		if (instance) {
			instance->strength = strength;
		}
	}

	bool Black()
	{
		return !imod || strength >= 1.0f;
	}

	void Reset()
	{
		instance.reset();
		strength = target = 0.0f;
		age = 0.0f;
	}
}
