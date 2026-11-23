#include "HUD.h"

#include "Lang.h"

namespace HUD
{
	namespace
	{
		constexpr auto  kHoldMode = "MovementDisabled";
		constexpr auto  kHintName = "ItemInspectionHint";
		constexpr auto  kHintPath = "_root.ItemInspectionHint";
		constexpr float kHintWidth = 1100.0f;
		constexpr float kHintMargin = 40.0f;  // from the screen's edges

		RE::GPtr<RE::GFxMovieView> HUDMovie()
		{
			const auto ui = RE::UI::GetSingleton();
			const auto menu = ui ? ui->GetMenu(RE::HUDMenu::MENU_NAME) : nullptr;
			return menu ? menu->uiMovie : nullptr;
		}

		template <class F>
		void OnUIThread(F&& a_func)
		{
			SKSE::GetTaskInterface()->AddUITask([func = std::forward<F>(a_func)]() {
				if (const auto movie = HUDMovie()) {
					func(*movie);
				}
			});
		}

		void AddShadow(RE::GFxMovieView& a_movie, RE::GFxValue& a_object)
		{
			// distance, angle, color, alpha, blurX, blurY, strength
			std::array<RE::GFxValue, 7> shadowArgs;
			for (std::size_t i = 0; const double v : { 1.5, 45.0, 0.0, 0.9, 3.0, 3.0, 1.5 }) {
				shadowArgs[i++].SetNumber(v);
			}
			RE::GFxValue shadow;
			a_movie.CreateObject(&shadow, "flash.filters.DropShadowFilter", shadowArgs.data(), static_cast<std::uint32_t>(shadowArgs.size()));
			if (shadow.IsObject()) {
				RE::GFxValue filters;
				a_movie.CreateArray(&filters);
				filters.PushBack(shadow);
				a_object.SetMember("filters", filters);
			}
		}

		// ---- the crosshair item info ----

		// The HUD's own rollover ("Take <name>", weight, value) is one of the elements our hold mode hides (vanilla HUD,
		// moreHUD's widgets follow the mode too). Nothing of the HUD's script is replaced: 3.0 put a filter in front of
		// SetCrosshairTarget, which froze the rollover for good in some load orders (moreHUD + BTPS).
		constexpr double kMaxRemovableDepth = 1048575.0;  // AS2 can't remove objects above this depth

		std::atomic<bool> holdMode{ false };          // game thread: our HUD mode should be on (read by the UI tasks)
		std::atomic<bool> rolloverWanted{ false };    // game thread: an item is held
		std::atomic<bool> refreshCrosshair{ false };  // game thread: publish the crosshair target again next frame

		// Better Third Person Selection draws its own crosshair widget (a copy of the HUD's rollover text, the HUD's own
		// one hidden); while an item is held it's hidden through BTPS's API (V0, its BTPS_API_decl.h)
		namespace BTPS
		{
			class API_V0
			{
			public:
				virtual bool GetSelectionEnabled() noexcept = 0;
				virtual bool GetWidget3DEnabled() noexcept = 0;
				virtual void HideSelectionWidget(std::string a_source) noexcept = 0;
				virtual void ShowSelectionWidget(std::string a_source) noexcept = 0;
			};

			constexpr auto kSource = "ItemInspection";  // short: no heap string handed to the other DLL

			API_V0* Get()
			{
				static API_V0* api{ nullptr };
				static bool    requested{ false };
				if (!requested) {
					requested = true;
					if (const auto module = GetModuleHandleW(L"BetterThirdPersonSelection.dll")) {
						using Request = void* (*)(int);
						if (const auto request = reinterpret_cast<Request>(GetProcAddress(module, "RequestPluginAPI"))) {
							api = static_cast<API_V0*>(request(0));
						}
						logs::info("Better Third Person Selection {}", api ? "found: its widget is hidden while an item is held" : "found without its API");
					}
				}
				return api;
			}
		}

		// a free depth on a_parent that AS2 can still remove (getNextHighestDepth can be above that when another mod put
		// something very high; then removeMovieClip / removeTextField do nothing and our text would stay for good)
		double RemovableDepth(RE::GFxValue& a_parent, double a_fallback)
		{
			RE::GFxValue next;
			a_parent.Invoke("getNextHighestDepth", &next);
			const double depth = next.IsNumber() ? next.GetNumber() : a_fallback;
			if (depth >= 0.0 && depth <= kMaxRemovableDepth) {
				return depth;
			}
			for (double candidate = kMaxRemovableDepth; candidate > kMaxRemovableDepth - 2000.0; candidate -= 1.0) {
				RE::GFxValue at;
				RE::GFxValue arg;
				arg.SetNumber(candidate);
				a_parent.Invoke("getInstanceAtDepth", &at, &arg, 1);
				if (at.IsUndefined() || at.IsNull()) {
					return candidate;
				}
			}
			return a_fallback;
		}

		void SetVisible(RE::GFxValue& a_object, bool a_visible)
		{
			RE::GFxValue value;
			value.SetBoolean(a_visible);
			a_object.SetMember("_visible", value);
		}

		// a text field on the HUD's root (placed by Layout), in the game's font with a shadow
		RE::GFxValue MakeHint(RE::GFxMovieView& a_movie)
		{
			RE::GFxValue field;
			a_movie.GetVariable(&field, kHintPath);
			if (field.IsDisplayObject()) {
				return field;
			}
			RE::GFxValue root;
			if (!a_movie.GetVariable(&root, "_root") || !root.IsObject()) {
				return {};
			}
			std::array<RE::GFxValue, 6> args;
			args[0].SetString(kHintName);
			args[1].SetNumber(RemovableDepth(root, 9000.0));
			args[2].SetNumber(0.0);
			args[3].SetNumber(0.0);
			args[4].SetNumber(kHintWidth);
			args[5].SetNumber(40.0);
			root.Invoke("createTextField", &field, args);
			if (!field.IsDisplayObject()) {
				a_movie.GetVariable(&field, kHintPath);  // some players don't return the new field
			}
			if (!field.IsDisplayObject()) {
				logs::warn("Couldn't add the hint to the HUD");
				return {};
			}
			RE::GFxValue value;
			value.SetBoolean(true);
			field.SetMember("embedFonts", value);
			value.SetBoolean(false);
			field.SetMember("selectable", value);

			AddShadow(a_movie, field);
			return field;
		}

		// where the hint is and how big, from the settings (each time it's shown: they can change in between)
		void Layout(RE::GFxMovieView& a_movie, RE::GFxValue& a_field, float a_x, float a_y, float a_size)
		{
			const auto   rect = a_movie.GetVisibleFrameRect();
			const double x = std::clamp(a_x, 0.0f, 100.0f) / 100.0;
			const double y = std::clamp(a_y, 0.0f, 100.0f) / 100.0;
			const double height = a_size * 2.0 + 4.0;
			const double left = rect.left + kHintMargin;
			const double top = rect.top + kHintMargin * 0.5;
			RE::GFxValue value;
			value.SetNumber(left + (rect.right - rect.left - 2.0 * kHintMargin - kHintWidth) * x);
			a_field.SetMember("_x", value);
			value.SetNumber(top + (rect.bottom - rect.top - kHintMargin - height) * y);
			a_field.SetMember("_y", value);
			value.SetNumber(height);
			a_field.SetMember("_height", value);

			RE::GFxValue format;
			a_movie.CreateObject(&format, "TextFormat");
			if (format.IsObject()) {
				value.SetString("$EverywhereMediumFont");
				format.SetMember("font", value);
				value.SetNumber(a_size);
				format.SetMember("size", value);
				value.SetNumber(0xFFFFFF);
				format.SetMember("color", value);
				value.SetString(x < 0.34 ? "left" : x > 0.66 ? "right" : "center");  // lines up with the side it's near
				format.SetMember("align", value);
				a_field.Invoke("setNewTextFormat", nullptr, &format, 1);
			}
		}

		// ---- the item info panel ----

		constexpr auto  kInfoName = "ItemInspectionInfo";
		constexpr auto  kInfoPath = "_root.ItemInspectionInfo";
		constexpr float kInfoWidth = 440.0f;
		constexpr float kInfoMargin = 70.0f;  // from the screen's side
		// item card types (the inventory's InventoryDefines.ICT_*)
		constexpr int kCardArmor = 1;
		constexpr int kCardWeapon = 2;

		void RemoveInfo(RE::GFxMovieView& a_movie)
		{
			RE::GFxValue clip;
			if (a_movie.GetVariable(&clip, kInfoPath) && clip.IsDisplayObject()) {
				SetVisible(clip, false);  // gone even if it can't be removed
				clip.Invoke("removeMovieClip");
			}
		}

		// the panel's clip, emptied (the one from before if it's still there)
		RE::GFxValue InfoClip(RE::GFxMovieView& a_movie, RE::GFxValue& a_root)
		{
			RE::GFxValue clip;
			if (a_movie.GetVariable(&clip, kInfoPath) && clip.IsDisplayObject()) {
				for (const auto name : { "NameText", "StatsText", "EffectsText" }) {
					RE::GFxValue field;
					if (clip.GetMember(name, &field) && field.IsDisplayObject()) {
						field.Invoke("removeTextField");
					}
				}
				clip.Invoke("clear");  // the line
				SetVisible(clip, true);
				return clip;
			}
			std::array<RE::GFxValue, 2> clipArgs;
			clipArgs[0].SetString(kInfoName);
			clipArgs[1].SetNumber(RemovableDepth(a_root, 9001.0));
			a_root.Invoke("createEmptyMovieClip", &clip, clipArgs);
			if (!clip.IsDisplayObject()) {
				a_movie.GetVariable(&clip, kInfoPath);
			}
			return clip;
		}

		std::string Escaped(std::string_view a_text)
		{
			std::string out;
			for (const char c : a_text) {
				switch (c) {
				case '&':
					out += "&amp;";
					break;
				case '<':
					out += "&lt;";
					break;
				case '>':
					out += "&gt;";
					break;
				default:
					out += c;
				}
			}
			return out;
		}

		std::optional<double> Number(const RE::GFxValue& a_card, const char* a_name)
		{
			RE::GFxValue value;
			if (a_card.GetMember(a_name, &value) && value.IsNumber()) {
				return value.GetNumber();
			}
			return std::nullopt;
		}

		std::string Text(const RE::GFxValue& a_card, const char* a_name)
		{
			RE::GFxValue value;
			if (a_card.GetMember(a_name, &value) && value.IsString()) {
				return value.GetString();
			}
			return {};
		}

		std::string FormatNumber(double a_value, bool a_decimals)
		{
			if (!a_decimals || std::abs(a_value - std::round(a_value)) < 0.05) {
				return std::format("{:.0f}", std::floor(a_value + 0.5));
			}
			return std::format("{:.1f}", a_value);
		}

		RE::GFxValue AddField(RE::GFxMovieView& a_movie, RE::GFxValue& a_clip, const char* a_name, int a_depth, double a_y, const char* a_align,
			const std::string& a_html)
		{
			std::array<RE::GFxValue, 6> args;
			args[0].SetString(a_name);
			args[1].SetNumber(a_depth);
			args[2].SetNumber(0.0);
			args[3].SetNumber(a_y);
			args[4].SetNumber(kInfoWidth);
			args[5].SetNumber(30.0);
			RE::GFxValue field;
			a_clip.Invoke("createTextField", &field, args);
			if (!field.IsDisplayObject()) {
				a_clip.GetMember(a_name, &field);
			}
			if (!field.IsDisplayObject()) {
				return {};
			}
			RE::GFxValue value;
			value.SetBoolean(true);
			field.SetMember("embedFonts", value);
			field.SetMember("multiline", value);
			field.SetMember("wordWrap", value);
			field.SetMember("html", value);
			value.SetBoolean(false);
			field.SetMember("selectable", value);
			value.SetString(a_align);
			field.SetMember("autoSize", value);  // grows down to fit, keeps its width
			RE::GFxValue html;
			a_movie.CreateString(&html, a_html.c_str());
			field.SetMember("htmlText", html);
			return field;
		}

		double Height(const RE::GFxValue& a_field)
		{
			RE::GFxValue value;
			return a_field.GetMember("_height", &value) && value.IsNumber() ? value.GetNumber() : 0.0;
		}
	}

	void SetHoldMode(bool a_on)
	{
		holdMode = a_on;  // before the task: a KeepHoldMode task still waiting won't push ours back after this
		OnUIThread([a_on](RE::GFxMovieView& a_movie) {
			std::array<RE::GFxValue, 2> args;
			args[0].SetString(kHoldMode);
			args[1].SetBoolean(a_on);
			if (!a_movie.Invoke("_root.HUDMovieBaseInstance.ShowElements", nullptr, args.data(), static_cast<std::uint32_t>(args.size()))) {
				logs::warn("The HUD has no ShowElements, its item info stays");
			}
		});
		HideRollover(a_on);  // Better Third Person Selection's copy of the crosshair item info
	}

	void KeepHoldMode()
	{
		OnUIThread([](RE::GFxMovieView& a_movie) {
			if (!holdMode.load()) {
				return;  // the hold ended after this was asked
			}
			RE::GFxValue modes;
			if (!a_movie.GetVariable(&modes, "_root.HUDMovieBaseInstance.HUDModes") || !modes.IsArray()) {
				return;
			}
			RE::GFxValue top;
			const auto   count = modes.GetArraySize();
			const bool known = count > 0 && modes.GetElement(count - 1, &top) && top.IsString();
			if (known && std::string_view{ top.GetString() } == kHoldMode) {
				return;
			}
			static bool logged{ false };
			if (!logged) {
				logs::info("HUD mode {} was on top of ours, put ours back over it", known ? top.GetString() : "(none)");
				logged = true;
			}
			std::array<RE::GFxValue, 2> args;
			args[0].SetString(kHoldMode);
			args[1].SetBoolean(true);
			a_movie.Invoke("_root.HUDMovieBaseInstance.ShowElements", nullptr, args.data(), static_cast<std::uint32_t>(args.size()));
		});
	}

	void ShowHint(const std::string& a_text, float a_x, float a_y, float a_size)
	{
		OnUIThread([a_text, a_x, a_y, a_size](RE::GFxMovieView& a_movie) {
			auto field = MakeHint(a_movie);
			if (!field.IsDisplayObject()) {
				return;
			}
			Layout(a_movie, field, a_x, a_y, a_size);
			RE::GFxValue text;
			a_movie.CreateString(&text, a_text.c_str());  // owned by the movie, unlike SetString
			field.SetMember("text", text);
			SetVisible(field, true);
		});
	}

	void HideHint()
	{
		OnUIThread([](RE::GFxMovieView& a_movie) {
			RE::GFxValue field;
			if (a_movie.GetVariable(&field, kHintPath) && field.IsDisplayObject()) {
				SetVisible(field, false);  // gone even if it can't be removed
				field.Invoke("removeTextField");
			}
		});
	}

	void HideRollover(bool a_hide)
	{
		if (rolloverWanted.exchange(a_hide) == a_hide) {
			return;
		}
		if (const auto btps = BTPS::Get()) {
			if (a_hide) {
				btps->HideSelectionWidget(BTPS::kSource);
			} else {
				btps->ShowSelectionWidget(BTPS::kSource);
			}
		}
		if (!a_hide) {
			refreshCrosshair = true;  // the game shows what's under the crosshair now (it only sends changes)
		}
	}

	void Update()
	{
		if (refreshCrosshair.exchange(false)) {
			// Skyrim's own crosshair publisher (what moreHUD calls after a HUD mode pop): fresh rollover data for the
			// current target to the HUD, moreHUD and Better Third Person Selection (its text hook is inside it)
			if (const auto player = RE::PlayerCharacter::GetSingleton()) {
				player->UpdateCrosshairs();
			}
		}
	}

	void ShowItemInfo(std::shared_ptr<RE::InventoryEntryData> a_entry, const std::string& a_name, const ItemInfoLayout& a_layout)
	{
		if (!a_entry || !a_entry->object) {
			return;
		}
		// the labels in the menu's language, read here (the UI task runs later)
		const std::string damageLabel = Lang::T("DAMAGE");
		const std::string armorLabel = Lang::T("ARMOR");
		const std::string weightLabel = Lang::T("WEIGHT");
		const std::string valueLabel = Lang::T("VALUE");
		OnUIThread([a_entry, a_name, a_layout, damageLabel, armorLabel, weightLabel, valueLabel](RE::GFxMovieView& a_movie) {
			// the inventory's own item card data for it
			RE::ItemCard card(&a_movie);
			card.SetItem(a_entry.get(), true);
			if (!card.obj.IsObject()) {
				RemoveInfo(a_movie);
				return;
			}
			const auto type = Number(card.obj, "type").value_or(0.0);
			std::string name = Text(card.obj, "name");
			if (name.empty()) {
				name = a_name;
			}

			const char* align = a_layout.right ? "right" : "left";
			const auto  stat = [](const std::string& a_label, const std::string& a_value) {
				return std::format("<font face='$EverywhereMediumFont' size='15' color='#9A9A9A'>{}</font> "
								   "<font face='$EverywhereMediumFont' size='24' color='#FFFFFF'>{}</font>",
					Escaped(a_label), a_value);
			};
			std::vector<std::string> stats;
			if (const auto damage = Number(card.obj, "damage"); damage && static_cast<int>(type) == kCardWeapon) {
				stats.push_back(stat(damageLabel, FormatNumber(*damage, false)));
			}
			if (const auto armor = Number(card.obj, "armor"); armor && static_cast<int>(type) == kCardArmor) {
				stats.push_back(stat(armorLabel, FormatNumber(*armor, false)));
			}
			if (const auto weight = Number(card.obj, "weight")) {
				stats.push_back(stat(weightLabel, FormatNumber(*weight, true)));
			}
			if (const auto value = Number(card.obj, "value")) {
				stats.push_back(stat(valueLabel, FormatNumber(*value, false)));
			}
			std::string statsHtml;
			for (const auto& line : stats) {
				statsHtml += (statsHtml.empty() ? "" : "&nbsp;&nbsp;&nbsp;&nbsp;") + line;
			}
			// enchantment / potion effects (already the card's html), else a book's or item's description
			std::string effects = Text(card.obj, "effects");
			if (effects.empty()) {
				effects = Text(card.obj, "description");
			}

			RE::GFxValue root;
			if (!a_movie.GetVariable(&root, "_root") || !root.IsObject()) {
				return;
			}
			auto clip = InfoClip(a_movie, root);
			if (!clip.IsDisplayObject()) {
				logs::warn("Couldn't add the item info to the HUD");
				return;
			}

			const auto nameField = AddField(a_movie, clip, "NameText", 1, 0.0, align,
				std::format("<p align='{}'><font face='$EverywhereMediumFont' size='28' color='#FFFFFF'>{}</font></p>", align, Escaped(name)));
			double y = Height(nameField) + 6.0;
			// a thin line under the name, like the inventory's card
			{
				std::array<RE::GFxValue, 3> style;
				style[0].SetNumber(1.0);
				style[1].SetNumber(0xFFFFFF);
				style[2].SetNumber(55.0);
				clip.Invoke("lineStyle", nullptr, style);
				std::array<RE::GFxValue, 2> point;
				point[0].SetNumber(a_layout.right ? kInfoWidth * 0.25 : 0.0);
				point[1].SetNumber(y);
				clip.Invoke("moveTo", nullptr, point);
				point[0].SetNumber(a_layout.right ? kInfoWidth : kInfoWidth * 0.75);
				clip.Invoke("lineTo", nullptr, point);
			}
			y += 8.0;
			if (!statsHtml.empty()) {
				const auto field = AddField(a_movie, clip, "StatsText", 2, y, align, std::format("<p align='{}'>{}</p>", align, statsHtml));
				y += Height(field) + 4.0;
			}
			if (!effects.empty()) {
				const auto field = AddField(a_movie, clip, "EffectsText", 3, y, align,
					std::format("<p align='{}'><font face='$EverywhereMediumFont' size='18' color='#D8D8D8'>{}</font></p>", align, effects));
				y += Height(field);
			}
			AddShadow(a_movie, clip);

			// placed at its side of the screen, scaled
			const auto   rect = a_movie.GetVisibleFrameRect();
			const double scale = std::clamp(a_layout.size, 0.3f, 3.0f);
			const double width = kInfoWidth * scale;
			const double height = y * scale;
			RE::GFxValue value;
			value.SetNumber(scale * 100.0);
			clip.SetMember("_xscale", value);
			clip.SetMember("_yscale", value);
			value.SetNumber(a_layout.right ? rect.right - kInfoMargin - width : rect.left + kInfoMargin);
			clip.SetMember("_x", value);
			const double top = std::clamp(a_layout.y, 0.0f, 100.0f) / 100.0;
			value.SetNumber(rect.top + kInfoMargin * 0.5 + (rect.bottom - rect.top - kInfoMargin - height) * top);
			clip.SetMember("_y", value);
		});
	}

	void HideItemInfo()
	{
		OnUIThread([](RE::GFxMovieView& a_movie) { RemoveInfo(a_movie); });
	}
}
