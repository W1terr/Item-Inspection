#include "Lang.h"

#include "Settings.h"

#include <atomic>
#include <fstream>
#include <unordered_map>

#include <nlohmann/json.hpp>

namespace Lang
{
	namespace
	{
		// English, French, German, Italian, Spanish, Polish, Russian, Japanese, Chinese (traditional). English is the code's
		// own text; the others are read from Data\SKSE\Plugins\ItemInspection\lang\<file>.json (English text -> translation),
		// so a translation can be fixed or added without a new DLL.
		constexpr std::array<const char*, kCount> kNativeNames{ "English", "Français", "Deutsch", "Italiano", "Español", "Polski",
			"Русский", "日本語", "繁體中文" };
		constexpr std::array<std::string_view, kCount> kGameNames{ "ENGLISH", "FRENCH", "GERMAN", "ITALIAN", "SPANISH", "POLISH",
			"RUSSIAN", "JAPANESE", "CHINESE" };
		constexpr std::array<std::string_view, kCount> kFiles{ "", "french", "german", "italian", "spanish", "polish", "russian",
			"japanese", "chinese" };

		struct StringHash
		{
			using is_transparent = void;
			std::size_t operator()(std::string_view a_text) const { return std::hash<std::string_view>{}(a_text); }
		};
		using Table = std::unordered_map<std::string, std::string, StringHash, std::equal_to<>>;

		// every language's texts, read once at the start (before the menu and the HUD read them from other threads);
		// a language change in the menu only switches `current`
		std::array<Table, kCount> tables;
		std::atomic<int>          current{ 1 };

		void LoadFile(int a_language)
		{
			auto&      table = tables[a_language - 1];
			const auto path = Settings::PluginFolder() / "ItemInspection" / "lang" / std::format("{}.json", kFiles[a_language - 1]);
			std::ifstream file(path);
			if (!file) {
				logs::warn("Missing {}: {} stays English", path.string(), kNativeNames[a_language - 1]);
				return;
			}
			try {
				const auto json = nlohmann::json::parse(file, nullptr, true, true);
				for (const auto& [english, text] : json.items()) {
					if (text.is_string() && !text.get_ref<const std::string&>().empty()) {
						table.emplace(english, text.get<std::string>());
					}
				}
			} catch (const std::exception& e) {
				logs::error("Couldn't read {}: {}", path.string(), e.what());
				table.clear();
			}
		}

		void LoadAll()
		{
			static const bool loaded = [] {
				for (int language = 2; language <= kCount; ++language) {
					LoadFile(language);
				}
				return true;
			}();
			(void)loaded;
		}

		int GameLanguage()
		{
			const auto ini = RE::INISettingCollection::GetSingleton();
			const auto setting = ini ? ini->GetSetting("sLanguage:General") : nullptr;
			const char* name = setting ? setting->GetString() : nullptr;
			if (name) {
				std::string upper{ name };
				std::ranges::transform(upper, upper.begin(), [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
				for (int i = 0; i < kCount; ++i) {
					if (upper == kGameNames[i]) {
						return i + 1;
					}
				}
			}
			return 1;
		}
	}

	void Set(int a_setting)
	{
		LoadAll();
		const int language = a_setting >= 1 && a_setting <= kCount ? a_setting : GameLanguage();
		current = language;
		logs::info("Language: {}{} ({} texts)", kNativeNames[language - 1], a_setting == kAuto ? " (the game's)" : "",
			language == 1 ? 0 : tables[language - 1].size());
	}

	int Current()
	{
		return current;
	}

	const char* NativeName(int a_language)
	{
		return a_language >= 1 && a_language <= kCount ? kNativeNames[a_language - 1] : "";
	}

	const char* T(const char* a_english)
	{
		const int language = current;
		if (language <= 1) {
			return a_english;
		}
		const auto& table = tables[language - 1];
		const auto  found = table.find(std::string_view(a_english));
		return found != table.end() ? found->second.c_str() : a_english;
	}
}
