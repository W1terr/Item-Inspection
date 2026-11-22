#include "HUD.h"

#include <atomic>

namespace HUD
{
	namespace
	{
		constexpr auto  kHoldMode = "MovementDisabled";
		constexpr auto  kHintName = "ItemInspectionHint";
		constexpr auto  kHintPath = "_root.ItemInspectionHint";
		constexpr float kHintWidth = 1100.0f;
		constexpr float kHintMargin = 40.0f;  // from the screen's edges

		std::atomic<bool> previewing{ false };

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
			RE::GFxValue depth;
			root.Invoke("getNextHighestDepth", &depth);
			std::array<RE::GFxValue, 6> args;
			args[0].SetString(kHintName);
			args[1].SetNumber(depth.IsNumber() ? depth.GetNumber() : 9000.0);
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
				field.SetMember("filters", filters);
			}
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
	}

	void SetHoldMode(bool a_on)
	{
		OnUIThread([a_on](RE::GFxMovieView& a_movie) {
			std::array<RE::GFxValue, 2> args;
			args[0].SetString(kHoldMode);
			args[1].SetBoolean(a_on);
			if (!a_movie.Invoke("_root.HUDMovieBaseInstance.ShowElements", nullptr, args.data(), static_cast<std::uint32_t>(args.size()))) {
				logs::warn("The HUD has no ShowElements, its item info stays");
			}
		});
	}

	void KeepHoldMode()
	{
		OnUIThread([](RE::GFxMovieView& a_movie) {
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
		previewing = false;
		OnUIThread([a_text, a_x, a_y, a_size](RE::GFxMovieView& a_movie) {
			auto field = MakeHint(a_movie);
			if (!field.IsDisplayObject()) {
				return;
			}
			Layout(a_movie, field, a_x, a_y, a_size);
			RE::GFxValue text;
			a_movie.CreateString(&text, a_text.c_str());  // owned by the movie, unlike SetString
			field.SetMember("text", text);
			RE::GFxValue visible;
			visible.SetBoolean(true);
			field.SetMember("_visible", visible);
		});
	}

	void HideHint()
	{
		previewing = false;
		OnUIThread([](RE::GFxMovieView& a_movie) {
			RE::GFxValue field;
			if (a_movie.GetVariable(&field, kHintPath) && field.IsDisplayObject()) {
				field.Invoke("removeTextField");
			}
		});
	}

	void PreviewHint(const std::string& a_text, float a_x, float a_y, float a_size)
	{
		ShowHint(a_text, a_x, a_y, a_size);
		previewing = true;
	}

	bool Previewing()
	{
		return previewing;
	}
}
