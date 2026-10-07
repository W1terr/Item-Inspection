#pragma once

// QuickLoot IE: items taken from containers with its loot menu go to the hand first ([QuickLoot] bQuickLoot). Its
// modder API (lib/QuickLootAPI) lets us stop a take and hide its menu while the item is held. Without QuickLoot IE
// all of this does nothing.
namespace QuickLoot
{
	void Connect();         // SKSE kPostLoad: find QuickLoot IE's interface and listen to its takes
	bool Installed();
	void Pause(bool a_pause);  // hides its loot menu while an item from a container is held; shown again (refreshed) after
}
