#include "BodyArm.h"

namespace BodyArm
{
	namespace
	{
		constexpr float kHidden = 0.001f;  // same as Improved Camera uses for the arms it hides

		RE::NiPointer<RE::NiAVObject> upperArm;
		float                         shownScale{ 1.0f };

		void Update(RE::NiAVObject* a_node)
		{
			RE::NiUpdateData update{};
			a_node->Update(update);
		}
	}

	void Hide(RE::NiAVObject* a_thirdPersonRoot)
	{
		if (!a_thirdPersonRoot) {
			return;
		}
		if (!upperArm) {
			const auto node = a_thirdPersonRoot->GetObjectByName("NPC R UpperArm [RUar]");
			if (!node || node->local.scale <= kHidden) {
				return;
			}
			upperArm.reset(node);
			shownScale = node->local.scale;
		}
		// checked every frame: another mod may write the scale back (the animation keeps it in the world data after that)
		if (upperArm->local.scale != kHidden) {
			upperArm->local.scale = kHidden;
			Update(upperArm.get());
		}
	}

	void Restore()
	{
		if (!upperArm) {
			return;
		}
		if (upperArm->local.scale == kHidden) {
			upperArm->local.scale = shownScale;
			Update(upperArm.get());
		}
		upperArm.reset();
	}

	void Forget()
	{
		// the node may already be gone with the old 3D: let go of our reference only
		upperArm.reset();
	}
}
