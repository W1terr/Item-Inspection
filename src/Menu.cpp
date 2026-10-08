#include "Menu.h"

#include "Keys.h"
#include "Lang.h"
#include "QuickLoot.h"
#include "Seen.h"
#include "Settings.h"
#include "StoreDisplay.h"

// third party header: keep its warnings out of our build
#pragma warning(push, 0)
#define _SILENCE_CXX17_CODECVT_HEADER_DEPRECATION_WARNING
#include "SKSEMenuFramework.h"
#pragma warning(pop)

namespace Menu
{
	namespace
	{
		namespace ImGui = ImGuiMCP;
		using Lang::T;

		bool dirty{ false };  // saved when the menu closes

		// key capture: the setting waiting for a key press, and whether it takes gamepad buttons
		int* capturing{ nullptr };
		bool captureGamepad{ false };

		// the names the menu entries were registered with, and a pending switch to the current language's names
		std::string sectionName;
		std::string settingsName;
		std::string languagesName;
		bool        renamePending{ false };

		// Newer framework versions can rename their entries (not in our copy of its header). False if the installed
		// framework can't: then the new names show after the next game start.
		bool RenameSection(const std::string& a_path, const std::string& a_newName)
		{
			using func_t = bool (*)(const char*, const char*);
			static const auto func = GetMenuFrameworkFunction<func_t>("RenameSection");
			return func && func(a_path.c_str(), a_newName.c_str());
		}

		// a translated label with an ID that stays the same in every language
		std::string Label(const char* a_english)
		{
			return std::format("{}##{}", T(a_english), a_english);
		}

		void Changed(bool a_changed)
		{
			if (a_changed) {
				dirty = true;
			}
		}

		void Help(const char* a_english)
		{
			if (ImGui::IsItemHovered()) {
				ImGui::SetTooltip("%s", T(a_english));
			}
		}

		void Header(const char* a_english)
		{
			ImGui::SeparatorText(T(a_english));
		}

		void Checkbox(const char* a_english, bool& a_value, const char* a_help = nullptr)
		{
			Changed(ImGui::Checkbox(Label(a_english).c_str(), &a_value));
			if (a_help) {
				Help(a_help);
			}
		}

		void Slider(const char* a_english, float& a_value, Settings::Range a_range, const char* a_format, const char* a_help = nullptr)
		{
			Changed(ImGui::SliderFloat(Label(a_english).c_str(), &a_value, a_range.min, a_range.max, a_format));
			a_value = std::clamp(a_value, a_range.min, a_range.max);
			if (a_help) {
				Help(a_help);
			}
		}

		void KeyButton(const char* a_id, int& a_code, bool a_gamepad)
		{
			const bool waiting = capturing == &a_code;
			const auto label = std::format("{}##{}", waiting ? std::string(T("Press a key...")) : Keys::Name(a_code), a_id);
			if (ImGui::Button(label.c_str(), ImGui::ImVec2(150.0f, 0.0f))) {
				capturing = waiting ? nullptr : &a_code;
				captureGamepad = a_gamepad;
			}
			Help(a_gamepad ? "Click, then press a gamepad button. Esc cancels." : "Click, then press a key or mouse button. Esc cancels.");
		}

		void KeyRow(const char* a_english, const char* a_id, int& a_keyboard, int& a_gamepad)
		{
			ImGui::Text("%s", T(a_english));
			ImGui::SameLine(190.0f);
			KeyButton(std::format("{}_kb", a_id).c_str(), a_keyboard, false);
			ImGui::SameLine();
			KeyButton(std::format("{}_pad", a_id).c_str(), a_gamepad, true);
		}

		void __stdcall RenderSettings()
		{
			auto& v = Settings::Edit();

			Header("General");
			Checkbox("Enabled", v.enabled, "Items you take from the world go to your hand first.");
			Checkbox("Show it every time", v.alwaysInspect,
				"Off: you look at each kind of item only the first time you take it in this playthrough.\n"
				"After that it goes straight into the inventory like in the normal game.\n"
				"On: every item you take goes to your hand first.");
			if (!v.alwaysInspect) {
				ImGui::Indent();
				const auto count = Seen::Count();
				const auto seen = std::vformat(T("Already looked at in this playthrough: {} kinds of items"), std::make_format_args(count));
				ImGui::Text("%s", seen.c_str());
				ImGui::SameLine();
				if (ImGui::Button(Label("Forget them").c_str())) {
					Seen::Clear();
				}
				Help("Every kind of item shows up in your hand again the next time you take it.");
				ImGui::Unindent();
			}
			Checkbox("Scroll up on an item in the inventory to take it in your hand", v.inventoryInspect,
				"Instead of the zoomed preview, the inventory closes and you hold the item.\nPutting it away opens the inventory again.");
			Checkbox("Look at items you steal too", v.inspectStolen,
				"On: stolen items go to your hand first as well. It only counts as stealing when you put the item\n"
				"in your backpack; putting it back is no crime.\nOff: stealing works like in the normal game.");
			Checkbox("Buy store items when you put them in the backpack", v.buyStoreItems,
				"With Purchaseable Store-Display-Items: an item for sale goes to your hand like any other.\n"
				"Putting it in the backpack asks you to buy it, with that mod's own message box and price;\n"
				"if you don't buy it, it is put back.\nOff: that mod asks right away, as usual.");
			if (v.buyStoreItems && !StoreDisplay::Installed()) {
				ImGui::Indent();
				ImGui::TextDisabled("%s", T("Purchaseable Store-Display-Items isn't installed: this has no effect."));
				ImGui::Unindent();
			}
			Checkbox("Telekinesis", v.telekinesis,
				"The item floats over your hand with the Telekinesis spell's hand effect,\nand your fingers move while you turn it.");
			if (v.telekinesis) {
				ImGui::Indent();
				Slider("Effect size", v.telekinesisSize, Settings::kEffectSize, "%.1f", "Size of the telekinesis effect on the hand. 1 = like the spell.");
				Checkbox("Hand light", v.telekinesisLight, "The spell's orange light around the hand.");
				Checkbox("Sound", v.telekinesisSound, "The telekinesis grab sound when you pick the item up, put it in the backpack or put it back.");
				ImGui::Unindent();
			}
			Checkbox("Item sound", v.itemSound, "The item's own pickup sound when it reaches your hand.");
			Checkbox("Show the key hint", v.showHint, "The keys to put the item away or back, on screen while you hold it.");
			Checkbox("Everyone waits for you", v.worldWaits,
				"While you hold an item nobody talks to you, attacks you or can hurt you:\npeople pause what they are doing until you put the item away.");
			Checkbox("Fade to black when the camera switches", v.fadeTransition,
				"From 3rd person: a short fade to black hides the switch to 1st person and back.");
			if (v.fadeTransition) {
				ImGui::Indent();
				Slider("Fade length", v.fadeTime, Settings::kFadeTime, "%.2f s", "Seconds to black; coming back from black takes a bit longer.");
				ImGui::Unindent();
			}
			ImGui::TextDisabled("%s", T("Weapons and bows are held by the grip; the mouse turns the hand. Other items float over the hand."));

			Header("Normal pickup for");
			Checkbox("Items taken during combat", v.skipInCombat);
			Checkbox("Gold", v.skipGold);
			Checkbox("Arrows and bolts", v.skipAmmo);
			Checkbox("Weapons", v.skipWeapons, "Swords, axes, bows... go straight into the inventory, like in the normal game.");
			Checkbox("Armor and clothes", v.skipArmor, "Armor, clothes and jewelry go straight into the inventory, like in the normal game.");
			Checkbox("Harvested plants", v.skipHarvest, "Ingredients from plants, mushrooms, nests... go straight into the inventory, like in the normal game.");

			Header("QuickLoot");
			Checkbox("Items taken with QuickLoot go to your hand", v.quickLoot,
				"When you take an item from a container or a body with QuickLoot's \"Take\", your character searches it first\n"
				"and then holds the item like any other. Put it in the backpack to take it, or put it back into the container.\n"
				"\"Take All\" and \"Equip\" / \"Use\" work as usual.");
			if (v.quickLoot) {
				ImGui::Indent();
				Slider("Search animation", v.searchTime, Settings::kSearchTime, "%.2f s",
					"Seconds your character searches the container first (the game's own searching animation).\n"
					"Only in 3rd person, 0 = no animation.");
				Slider("Search again (same container)", v.searchAgainTime, Settings::kSearchTime, "%.2f s",
					"Seconds of searching when you take another item from the container you just searched:\n"
					"a quick reach in. Until you look at another container. 0 = no animation.");
				Checkbox("Only big chests", v.bigChestsOnly,
					"Only items from real chests (wooden, noble, Dwemer, Falmer, ruins chests) go to your hand.\n"
					"Barrels, sacks, urns, drawers, wardrobes and bodies work as usual.");
				if (!v.bigChestsOnly) {
					Checkbox("Also from bodies", v.lootBodies,
						"Items you take from dead bodies (and corpses lying around) go to your hand too.\n"
						"Off: those are taken as usual.");
				}
				ImGui::Unindent();
			}
			if (!QuickLoot::Installed()) {
				ImGui::TextDisabled("%s", T("QuickLoot IE isn't installed: this has no effect."));
			}

			Header("Controls");
			ImGui::Text(" ");
			ImGui::SameLine(190.0f);
			ImGui::Text("%s", T("Keyboard / mouse"));
			ImGui::SameLine(190.0f + 158.0f);
			ImGui::Text("%s", T("Gamepad"));
			KeyRow("Put in backpack", "store", v.storeKey, v.storeGamepadKey);
			KeyRow("Put back", "putback", v.putBackKey, v.putBackGamepadKey);
			KeyRow("Look around (hold)", "look", v.lookKey, v.lookGamepadKey);
			KeyRow("Switch view", "switchview", v.switchViewKey, v.switchViewGamepadKey);
			if (v.holdKeyToInspect) {
				KeyRow("Inspect key (hold)", "inspect", v.inspectKey, v.inspectGamepadKey);
			}
			Checkbox("Hold a key to inspect", v.holdKeyToInspect,
				"On: an item goes to your hand only if you hold the inspect key while you press Activate\n"
				"(picking up, harvesting, QuickLoot's Take). A normal press picks it up as usual.");
			Slider("Mouse speed", v.mouseSensitivity, Settings::kSensitivity, "%.2f", "How fast moving the mouse turns the item.");
			Slider("Gamepad speed", v.gamepadSensitivity, Settings::kSensitivity, "%.2f", "How fast the right stick turns the item.");
			Checkbox("Invert up / down", v.invertY);
			Checkbox("Invert looking up / down", v.invertLookY);
			ImGui::TextDisabled("%s", T("The mouse wheel brings the item closer or moves it farther away.\n"
										"Hold the look key to turn your head; the hand and the item stay where they are.\n"
										"Let go and your head turns back to the item."));

			Header("Hand position (game units from your eyes)");
			Slider("Right", v.holdRight, Settings::kHoldRight, "%.1f", "Game units to the right of your eyes.");
			Slider("Forward", v.holdForward, Settings::kHoldForward, "%.1f", "Game units in front of your eyes.");
			Slider("Up", v.holdUp, Settings::kHoldUp, "%.1f", "Game units above (negative: below) your eyes.");
			Slider("Height above the hand", v.holdHeight, Settings::kHoldHeight, "%.1f", "How far the item floats above your palm.");

			Header("Item position");
			Slider("Item right", v.itemRight, Settings::kItemOffset, "%.1f", "Moves the item to the right (+) or left (-) of its place over the hand.");
			Slider("Item forward", v.itemForward, Settings::kItemOffset, "%.1f", "Moves the item away from you (+) or closer (-).");
			Slider("Item up", v.itemUp, Settings::kItemOffset, "%.1f", "Moves the item up (+) or down (-).");
			Slider("Largest item size", v.maxItemSize, Settings::kMaxItemSize, "%.1f",
				"Bigger items (shields, armor...) are shown smaller so they fit in the hand. Weapons keep their size.");

			Header("Hand rotation (degrees)");
			Slider("Turn", v.handTurn, Settings::kHandAngle, "%.0f", "Turns the hand to the right (+) or left (-).");
			Slider("Tilt", v.handTilt, Settings::kHandAngle, "%.0f", "Tilts the fingers up (+) or down (-).");
			Slider("Roll", v.handRoll, Settings::kHandAngle, "%.0f", "Rolls the palm around the fingers.");

			Header("Weapons (held by the grip)");
			Slider("Weapon hand right", v.weaponRight, Settings::kHoldRight, "%.1f", "Game units to the right of your eyes.");
			Slider("Weapon hand forward", v.weaponForward, Settings::kHoldForward, "%.1f", "Game units in front of your eyes.");
			Slider("Weapon hand up", v.weaponUp, Settings::kHoldUp, "%.1f", "Game units above (negative: below) your eyes.");
			Slider("Blade lean left", v.weaponLeanLeft, Settings::kHandAngle, "%.0f", "Degrees the blade tips to the left (-: right) from straight up.");
			Slider("Blade lean forward", v.weaponLeanForward, Settings::kHandAngle, "%.0f", "Degrees the blade tips away from you (-: towards you).");
			Slider("Blade roll", v.weaponRoll, Settings::kHandAngle, "%.0f", "Turns the weapon around the blade.");
			Slider("Hand along the handle", v.gripSlide, Settings::kGripSlide, "%.1f",
				"Slides your hand along the weapon's handle: + towards the pommel, - towards the blade.\n"
				"For weapons your hand holds at the wrong place, e.g. at the crossguard.");
			Slider("Turn in the hand", v.gripTurn, Settings::kHandAngle, "%.0f", "Turns the weapon around its blade inside your fist, in degrees.");
			Slider("Tilt in the hand", v.gripTilt, Settings::kGripAngle, "%.0f", "Tilts the weapon inside your fist, in degrees.");

			Header("Bows and crossbows");
			Slider("Bow hand right", v.bowRight, Settings::kHoldRight, "%.1f", "Game units to the right of your eyes.");
			Slider("Bow hand forward", v.bowForward, Settings::kHoldForward, "%.1f",
				"Game units in front of your eyes. Your arm reaches about 36, further it stays stretched.");
			Slider("Bow hand up", v.bowUp, Settings::kHoldUp, "%.1f", "Game units above (negative: below) your eyes.");
			Slider("Bow lean left", v.bowLeanLeft, Settings::kHandAngle, "%.0f", "Degrees the bow tips to the left (-: right) from straight up.");
			Slider("Bow lean forward", v.bowLeanForward, Settings::kHandAngle, "%.0f", "Degrees the bow tips away from you (-: towards you).");

			Header("Third person");
			Checkbox("Stay in third person", v.thirdPerson,
				"In 3rd person the camera moves in over your shoulder instead of switching to 1st person,\n"
				"and you see your hand put the item away in a pocket at your hip.");
			if (v.thirdPerson) {
				ImGui::Indent();
				Slider("Camera right", v.cameraRight, Settings::kCameraSide, "%.1f", "Game units the camera is to the right of your eyes.");
				Slider("Camera back", v.cameraBack, Settings::kCameraBack, "%.1f", "Game units the camera is behind your eyes.");
				Slider("Camera up", v.cameraUp, Settings::kCameraUp, "%.1f", "Game units the camera is above (negative: below) your eyes.");
				Slider("Camera move time", v.cameraTime, Settings::kTime, "%.2f s", "Seconds the camera takes to fly in and back.");
				Checkbox("Stand still while holding", v.standStill,
					"Your character goes back to standing and holds still while looking at the item,\n"
					"instead of playing idle animations.");
				Checkbox("Look at the item", v.headLook, "Your character's head turns to look at the item in the hand.");
				Slider("Hand right (3rd person)", v.bodyHoldRight, Settings::kHoldRight, "%.1f", "Game units to the right of your eyes.");
				Slider("Hand forward (3rd person)", v.bodyHoldForward, Settings::kHoldForward, "%.1f", "Game units in front of your eyes.");
				Slider("Hand up (3rd person)", v.bodyHoldUp, Settings::kBodyHoldUp, "%.1f", "Game units above (negative: below) your eyes.");
				Slider("Item size (3rd person)", v.bodyItemScale, Settings::kItemScale, "%.2f", "Items (not weapons) are shown this much smaller in 3rd person.");
				Slider("Item right (3rd person)", v.bodyItemRight, Settings::kItemOffset, "%.1f", "Moves the item to the right (-: left) as the camera sees it.");
				Slider("Item forward (3rd person)", v.bodyItemForward, Settings::kItemOffset, "%.1f", "Moves the item away from the camera (-: towards it).");
				Slider("Item up (3rd person)", v.bodyItemUp, Settings::kItemOffset, "%.1f", "Moves the item up (-: down) as the camera sees it.");
				Slider("Weapon: hand along the handle (3rd person)", v.bodyGripSlide, Settings::kGripSlide, "%.1f",
					"Slides your hand along the weapon's handle: + towards the pommel, - towards the blade.\n"
					"For weapons your hand holds at the wrong place, e.g. at the crossguard.");
				Slider("Weapon: turn in the hand (3rd person)", v.bodyGripTurn, Settings::kHandAngle, "%.0f", "Turns the weapon around its blade inside your fist, in degrees.");
				Slider("Weapon: tilt in the hand (3rd person)", v.bodyGripTilt, Settings::kGripAngle, "%.0f", "Tilts the weapon inside your fist, in degrees.");
				ImGui::Unindent();
			}
			Slider("Wait for the pickup animation", v.animationWait, Settings::kWait, "%.2f s",
				"Seconds a pickup in 3rd person waits, so the pickup animation of Immersive Interactions\n"
				"plays first. Only when that mod is installed, 0 = don't wait.");

			Header("Animation (seconds)");
			Slider("Hand comes up", v.raiseTime, Settings::kTime, "%.2f");
			Slider("Hand goes to the backpack", v.stowTime, Settings::kTime, "%.2f");
			Slider("Hand comes back", v.returnTime, Settings::kTime, "%.2f");

			ImGui::Spacing();
			if (ImGui::Button(Label("Reset to defaults").c_str())) {
				const int language = v.language;  // the language is set on its own page
				v = Settings::Values{};
				v.language = language;
				dirty = true;
			}
		}

		void __stdcall RenderLanguages()
		{
			auto& v = Settings::Edit();
			Header("Language");
			int choice = v.language;
			bool changed = ImGui::RadioButton(Label("Automatic (game language)").c_str(), &choice, Lang::kAuto);
			for (int language = 1; language <= Lang::kCount; ++language) {
				changed |= ImGui::RadioButton(Lang::NativeName(language), &choice, language);
			}
			if (changed && choice != v.language) {
				v.language = choice;
				Lang::Set(choice);
				dirty = true;
				renamePending = true;  // the menu entries get their new names before the next frame
			}
			ImGui::Spacing();
			ImGui::TextWrapped("%s", T("The key hint on screen uses the game's own font: with a language the game isn't set to, some letters may be missing there."));
			// in English on purpose: without these glyphs a translation of it couldn't be read
			ImGui::TextDisabled("Polish, Russian, Japanese and Chinese need their characters turned on in SKSE Menu Framework:\n"
								"Options > Open Settings > Character Glyphs.");
		}

		// the menu entries in the current language: pages first (their path still uses the old section name), then the section
		void ApplyNames()
		{
			const std::string section = T("Item Inspection");
			const std::string settings = T("Settings");
			const std::string languages = T("Languages");
			bool ok = true;
			if (settings != settingsName) {
				ok &= RenameSection(sectionName + "/" + settingsName, settings);
			}
			if (ok && languages != languagesName) {
				ok &= RenameSection(sectionName + "/" + languagesName, languages);
			}
			if (ok && section != sectionName) {
				ok &= RenameSection(sectionName, section);
			}
			if (!ok) {
				logs::info("This SKSE Menu Framework can't rename its entries: the new names show after the next game start");
				return;
			}
			sectionName = section;
			settingsName = settings;
			languagesName = languages;
			SKSEMenuFramework::SetSection(sectionName);
		}

		// key capture for the key buttons; true = the game doesn't get this input
		bool __stdcall OnInput(RE::InputEvent* a_event)
		{
			if (!capturing) {
				return false;
			}
			for (auto event = a_event; event; event = event->next) {
				const auto button = event->AsButtonEvent();
				if (!button || !button->IsDown()) {
					continue;
				}
				const int code = Keys::Code(button);
				if (code == 0x01) {  // Esc
					capturing = nullptr;
					return true;
				}
				if (code == Keys::kNone || Keys::IsGamepad(code) != captureGamepad) {
					continue;
				}
				*capturing = code;
				capturing = nullptr;
				dirty = true;
				return true;
			}
			return false;
		}

		void __stdcall OnMenuEvent(SKSEMenuFramework::Model::EventType a_type)
		{
			switch (a_type) {
			case SKSEMenuFramework::Model::EventType::kBeforeRender:
				if (renamePending) {
					renamePending = false;
					ApplyNames();
				}
				break;
			case SKSEMenuFramework::Model::EventType::kCloseMenu:
				capturing = nullptr;
				if (dirty) {
					Settings::Save();
					dirty = false;
					logs::info("Settings saved");
				}
				break;
			default:
				break;
			}
		}
	}

	void Register()
	{
		if (!SKSEMenuFramework::IsInstalled()) {
			logs::info("SKSE Menu Framework not installed: settings only in the INI");
			return;
		}
		sectionName = T("Item Inspection");
		settingsName = T("Settings");
		languagesName = T("Languages");
		SKSEMenuFramework::SetSection(sectionName);
		SKSEMenuFramework::AddSectionItem(settingsName, RenderSettings);
		SKSEMenuFramework::AddSectionItem(languagesName, RenderLanguages);
		SKSEMenuFramework::AddInputEvent(OnInput);
		SKSEMenuFramework::AddEvent(OnMenuEvent, 0.0f);
		logs::info("Settings page added to SKSE Menu Framework");
	}
}
