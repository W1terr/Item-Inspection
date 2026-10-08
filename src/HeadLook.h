#pragma once

// 3rd person: the character's head (and a little of the neck) turns to look at the held item. Written over the
// animation's pose right after the arm is posed, blended in by a weight, within a natural range of turn.
// Bones the animation didn't rewrite since our last call keep their animated values from our own cache, so nothing
// stacks while the body's animation is stopped.
namespace HeadLook
{
	// a_root: the player's 3rd person body. a_target: world point to look at. a_facing: the player's facing (heading
	// direction, horizontal), the frame the turn limits are measured in. a_weight: 0 = animation, 1 = looking at it.
	void Apply(RE::NiAVObject* a_root, const RE::NiPoint3& a_target, const RE::NiPoint3& a_facing, float a_weight);

	// Gives the bones back to the animation (writes the animated values back once) and forgets them
	void Release();

	// The body was unloaded (game load): drop the cached bones without touching them
	void Forget();
}
