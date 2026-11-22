#pragma once

// A pose held over while the animation jumps: the skeleton's bones ("NPC ..." nodes: no physics, weapon or helper
// nodes) as they were at Capture, eased into whatever the animation does since. Used when a held weapon is equipped:
// its draw is played fast under it, and the body flows from the hold into the combat idle instead of snapping there.
namespace PoseBlend
{
	void Capture(RE::NiAVObject* a_root);
	// a_weight: how much of the animation shows (0 = the captured pose, 1 = only the animation). Call right after the
	// animation posed the skeleton (before our arm), every frame; updates the skeleton's world transforms.
	// False if nothing was captured for this skeleton.
	bool Apply(RE::NiAVObject* a_root, float a_weight);
	void Release();
}
