#pragma once

// The vanilla Telekinesis spell's look on the 1st person right hand while the item floats over it: its hand effect
// (casting art, scaled up, kept on the palm), its hand light, and its grab sound (played on its own when the item
// is picked up, put away or put back: the spell's cast loop sounded broken, more so with sound replacers). Purely
// visual: the spell isn't cast or needed.
namespace Telekinesis
{
	struct Options
	{
		float size{ 2.0f };  // scale of the hand effect
		bool  light{ true };
	};

	void Start(RE::NiAVObject* a_handNode, const Options& a_options);
	void Stop();
	void PlayGrabSound(RE::NiAVObject* a_node);  // follows the node
	void Place(const RE::NiTransform& a_world);  // every frame after the arm is posed: where the hand effect sits
	void Update(float a_delta);  // every frame: picks up effects the game created late, ends stopped ones
	void Forget();               // game load: the effects are gone with the old 3D
}
