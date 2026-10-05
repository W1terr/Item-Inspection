#pragma once

// Picking an item up from the world: instead of going straight to the inventory, the camera goes to 1st person, the
// right hand brings the item up in front of you, the mouse turns it, and a key puts it in the backpack (hand moves
// down and behind the back) or puts it back where it was.
namespace Inspect
{
	void InstallHooks();
	void RegisterInput();  // after the input devices exist (kInputLoaded)
	void Reset();          // game load / new game: drop a running inspection without picking up
}
