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
	// the settings page's preview of the hint: shown like ShowHint, stays until HideHint (or a real hint replaces it)
	void PreviewHint(const std::string& a_text, float a_x, float a_y, float a_size);
	bool Previewing();
}
