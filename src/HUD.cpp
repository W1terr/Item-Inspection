#include "HUD.h"

namespace HUD
{
	namespace
	{
		constexpr auto  kHoldMode = "MovementDisabled";
		constexpr auto  kHintName = "ItemInspectionHint";
		constexpr auto  kHintPath = "_root.ItemInspectionHint";
		constexpr float kHintWidth = 900.0f;
		constexpr float kHintHeight = 40.0f;

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

		// a text field on the HUD's root, top right like the HUD's messages, in the game's font with a shadow
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
			const auto            rect = a_movie.GetVisibleFrameRect();
			std::array<RE::GFxValue, 6> args;
			args[0].SetString(kHintName);
			args[1].SetNumber(depth.IsNumber() ? depth.GetNumber() : 9000.0);
			args[2].SetNumber(rect.right - kHintWidth - 40.0f);
			args[3].SetNumber(rect.top + 50.0f);
			args[4].SetNumber(kHintWidth);
			args[5].SetNumber(kHintHeight);
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

			RE::GFxValue format;
			a_movie.CreateObject(&format, "TextFormat");
			if (format.IsObject()) {
				value.SetString("$EverywhereMediumFont");
				format.SetMember("font", value);
				value.SetNumber(18.0);
				format.SetMember("size", value);
				value.SetNumber(0xFFFFFF);
				format.SetMember("color", value);
				value.SetString("right");
				format.SetMember("align", value);
				field.Invoke("setNewTextFormat", nullptr, &format, 1);
			}
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

	void ShowHint(const std::string& a_text)
	{
		OnUIThread([a_text](RE::GFxMovieView& a_movie) {
			auto field = MakeHint(a_movie);
			if (!field.IsDisplayObject()) {
				return;
			}
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
		OnUIThread([](RE::GFxMovieView& a_movie) {
			RE::GFxValue field;
			if (a_movie.GetVariable(&field, kHintPath) && field.IsDisplayObject()) {
				field.Invoke("removeTextField");
			}
		});
	}
}
