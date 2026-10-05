#pragma once

// Kinds of items (base forms) the player already looked at in this playthrough. Stored in the SKSE co-save,
// so every save / character has its own list.
namespace Seen
{
	void Register();  // SKSE serialization callbacks, call at plugin load

	bool        Contains(RE::FormID a_baseID);
	void        Add(RE::FormID a_baseID);
	std::size_t Count();
	void        Clear();
}
