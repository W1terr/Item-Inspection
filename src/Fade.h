#pragma once

// Short fades to and from black (camera switches). Uses the vanilla "FadeToBlackHoldImod" image space modifier with
// its strength driven by us, so any fade length works.
namespace Fade
{
	void To(float a_black, float a_seconds);  // a_black 1 = black, 0 = clear
	void Update(float a_delta);
	bool Black();  // fully black now (or fading isn't available)
	void Reset();  // game load: drop the instance without touching it
}
