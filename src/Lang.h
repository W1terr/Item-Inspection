#pragma once

// Menu and hint texts in the languages Skyrim SE ships in. Texts are looked up by their English wording; anything
// without a translation stays English.
namespace Lang
{
	inline constexpr int kAuto = 0;   // the game's own language (sLanguage:General)
	inline constexpr int kCount = 9;  // English, French, German, Italian, Spanish, Polish, Russian, Japanese, Chinese

	void        Set(int a_setting);  // kAuto or 1..kCount (the INI's iLanguage)
	int         Current();           // the language in use, 1..kCount
	const char* NativeName(int a_language);
	const char* T(const char* a_english);
}
