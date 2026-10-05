#pragma once

// Procedural pose of the 1st person right arm: two-bone IK (upper arm, forearm) to a wrist target, the hand turned to a
// wanted finger / palm direction, the wrist twist spread over the forearm twist bones, fingers curled a little.
// The result is blended over the animation's pose by a weight, so the arm moves smoothly in and out of it.
//
// Must run after the animation wrote the bones of this frame (we use the 1st person camera update for that).
// Only rotations are changed. Bones the animation didn't rewrite since our last call keep their animated values from
// our own cache, so calling it several times per frame doesn't stack.
namespace ArmPose
{
	struct Goal
	{
		RE::NiPoint3 wrist;      // world position for the hand bone (wrist)
		RE::NiPoint3 pole;       // world direction the elbow bends towards
		RE::NiPoint3 fingers;    // world direction the fingers point to
		RE::NiPoint3 palm;       // world direction the palm faces
		std::optional<RE::NiMatrix3> hand;  // if set: the hand's world rotation (instead of fingers / palm)
		float        weight{ 0.0f };  // 0 = animation, 1 = this goal
		std::array<float, 5> curl{};  // extra bend per joint for each finger (thumb first), radians
		bool                 weaponGrip{ false };  // fingers in the game's own grip around a weapon handle (instead of curl)
	};

	struct Hand
	{
		RE::NiTransform world;        // hand bone after posing
		RE::NiPoint3    palmCenter;   // world
		RE::NiPoint3    palmNormal;   // world, out of the palm
	};

	// The hand bone's own axes (hand local space), from the finger bones
	struct HandFrame
	{
		RE::NiPoint3                   fingerAxis;  // towards the middle finger
		RE::NiPoint3                   palmAxis;    // out of the palm
		float                          palmLength{ 0.0f };
		std::optional<RE::NiTransform> weaponGrip;  // the WEAPON node (where held weapons sit) relative to the hand
	};
	std::optional<HandFrame> Frame(RE::NiAVObject* a_root);

	// a_root: the player's 1st person skeleton. Returns the posed hand, nothing if the skeleton lacks the bones.
	std::optional<Hand> Apply(RE::NiAVObject* a_root, const Goal& a_goal);

	// Gives the bones back to the animation (writes the animated values back once) and forgets them
	void Release();

	// The skeleton was unloaded (game load): drop the cached bones without touching them
	void Forget();
}
