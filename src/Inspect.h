#pragma once

// Picking an item up from the world: instead of going straight to the inventory, the camera goes to 1st person, the
// right hand brings the item up in front of you, the mouse turns it, and a key puts it in the backpack (hand moves
// down and behind the back; in 3rd person into a pocket at the hip) or puts it back where it was.
namespace Inspect
{
	void InstallHooks();
	void RegisterInput();  // after the input devices exist (kInputLoaded)
	void Reset();          // game load / new game: drop a running inspection without picking up

	// A stack QuickLoot was about to take out of a container (or a body), read while its list entry was still valid
	struct ContainerItem
	{
		RE::ObjectRefHandle container;
		RE::TESBoundObject* object{ nullptr };
		std::int32_t        count{ 1 };
		RE::ExtraDataList*  extraList{ nullptr };  // the container's own list for this stack (enchantment, temper...), may be null
		bool                stealing{ false };
		std::int32_t        value{ 0 };
	};

	// QuickLoot's "Take": true = the item goes to the hand first (QuickLoot must not take it); the item moves to the
	// inventory only when it's put in the backpack. Called from QuickLoot's menu, the inspection starts as a task.
	bool OfferContainerItem(const ContainerItem& a_item);

	// QuickLoot shows a container: another one than the one searched last means the next take searches anew
	void ContainerShown(RE::ObjectRefHandle a_container);
}
