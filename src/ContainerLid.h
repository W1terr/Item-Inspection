#pragma once

// A container's lid (chests, barrels with lids...) stays open while an item taken out of it is held. QuickLoot opens
// it on "Take" and closes it when its menu hides, which it does while we hold the item: closing that container is
// held back (hook on the game's BGSOpenCloseForm::SetOpenState) until the item is put away or back.
namespace ContainerLid
{
	void Install();
	void KeepOpen(RE::TESObjectREFR* a_container);  // opens it if it's closed
	void Release(RE::ObjectRefHandle a_container);  // closes it again (still opening: once it's open)
	void Update(float a_delta);                     // every frame: a close that waits for the lid to be open
	void Forget();                                  // game load: nothing to close
}
