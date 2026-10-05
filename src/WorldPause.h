#pragma once

// "Everyone waits for you" while an item is held: NPC AI paused (like the console's tai), no combat AI (tcai), no
// detection (no greetings, nobody notices you), and the player can't be hurt. Everything is put back as it was.
namespace WorldPause
{
	void Begin();
	void End();
	void Reset();  // game load: put the switches back
}
