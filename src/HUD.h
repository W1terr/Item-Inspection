#pragma once

// The HUD while an item is held, done in the HUD movie itself (on the UI thread):
// - the HUD mode the game uses while it holds the player, through the HUD script's own ShowElements: only messages,
//   quest updates and subtitles stay, the crosshair's item info, compass and bars go away (vanilla and Edge UI alike)
// - the key hint as our own text field, so it stays until we take it away (HUD messages fade after a few seconds)
namespace HUD
{
	void SetHoldMode(bool a_on);
	void KeepHoldMode();  // every frame while holding: the game puts its own mode back on top, ours goes back over it
	// a_x / a_y: percent of the screen from the left / top (0 = at the left / top edge, 100 = at the right / bottom
	// edge); the text lines up with that side (left, centered, right). a_size: font size.
	void ShowHint(const std::string& a_text, float a_x, float a_y, float a_size);
	void HideHint();
	// Better Third Person Selection's crosshair widget (its copy of the HUD's item info, which our hold mode hides) is
	// hidden through its API while on; off has the game publish the crosshair target again
	void HideRollover(bool a_hide);
	void Update();  // every frame (game thread): the crosshair refresh after a hold

	// A panel at the side of the screen with what the inventory's item card shows for the item: name, damage or armor
	// (with the player's perks and skills), weight, value, enchantment / effects text. Filled by the game's own item card.
	struct ItemInfoLayout
	{
		bool  right{ true };  // at the right edge, else the left one
		float y{ 40.0f };     // percent of the screen from the top (0 = top, 100 = bottom)
		float size{ 1.0f };   // scale
	};
	void ShowItemInfo(std::shared_ptr<RE::InventoryEntryData> a_entry, const std::string& a_name, const ItemInfoLayout& a_layout);
	void HideItemInfo();
}
