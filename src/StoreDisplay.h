#pragma once

// Purchaseable Store-Display-Items (PSDI, Nexus 36005): items on display in shops can be bought by activating them
// (its perk shows "Buy", then a message box with the price). With it, such an item goes to the hand first like any
// other; only putting it in the backpack runs PSDI's own buy (its box, its price, the gold to the vendor). Not bought,
// it's put back. Without PSDI all of this does nothing.
namespace StoreDisplay
{
	bool Installed();  // PSDI's plugin is loaded (after the game's data is loaded)

	// PSDI offers to buy this item right now (the crosshair shows its "Buy")
	bool ForSale(RE::TESObjectREFR* a_ref);

	// Runs PSDI's buy for the item: its message box with the price; bought, the item is paid for, becomes the
	// player's and is picked up by PSDI's script. a_done runs as a game task when the script is finished, bought or not.
	bool Buy(RE::TESObjectREFR* a_ref, std::function<void()> a_done);
}
