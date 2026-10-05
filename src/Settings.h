#pragma once

// Data\SKSE\Plugins\ItemInspection.ini, edited in game on the SKSE Menu Framework page (src/Menu.cpp)
namespace Settings
{
	struct Values
	{
		// [General]
		bool enabled{ true };
		bool alwaysInspect{ false };    // false: only the first time each kind of item is taken in this playthrough
		bool inventoryInspect{ true };  // scrolling up on an item in the inventory takes it in the hand
		bool inspectStolen{ true };     // also items you steal (the crime happens when it goes into the backpack)
		bool telekinesis{ true };       // the item floats over the hand with the telekinesis hand effect
		bool telekinesisLight{ true };  // the spell's orange hand light
		bool telekinesisSound{ true };  // the grab sound when the item is picked up, put away or put back
		float telekinesisSize{ 2.0f };  // scale of the hand effect (vanilla 1 is barely visible)
		bool skipInCombat{ true };      // normal pickup while in combat
		bool skipGold{ true };
		bool skipAmmo{ true };          // arrows / bolts
		bool skipWeapons{ false };      // weapons (bows too) go straight into the inventory
		bool skipArmor{ false };        // armor, clothes and jewelry go straight into the inventory
		bool showHint{ true };          // "[E] Put in backpack  [R] Put back" on screen while holding
		bool worldWaits{ true };        // nobody talks to you, attacks or hurts you while you hold an item (AI paused)
		bool fadeTransition{ true };    // short fade to black when the camera switches between 3rd and 1st person
		float fadeTime{ 0.2f };         // seconds to black (back is 1.5x)
		int   language{ 0 };            // 0 = the game's language, 1..9 = English, French, German, Italian, Spanish, Polish, Russian, Japanese, Chinese

		// [Controls] keys: keyboard = DirectInput scan code, mouse = 256 + button, gamepad = SKSE code 266-281
		int   storeKey{ 18 };            // E
		int   putBackKey{ 19 };          // R
		int   storeGamepadKey{ 276 };    // A
		int   putBackGamepadKey{ 277 };  // B
		int   lookKey{ 257 };            // right mouse: held, the mouse turns the head (hand and item stay), released it turns back
		int   lookGamepadKey{ 281 };     // RT: held, the right stick turns the head
		bool  invertLookY{ false };
		float mouseSensitivity{ 1.0f };
		float gamepadSensitivity{ 1.0f };
		bool  invertY{ false };

		// [Hold] where the hand holds the item, in game units from the eye (right, forward, up)
		float holdRight{ 10.5f };
		float holdForward{ 31.4f };
		float holdUp{ -12.1f };
		float holdHeight{ 9.1f };    // how far the item floats above the palm
		float itemRight{ -1.3f };    // item offset from its place over the palm, eye space
		float itemForward{ 5.7f };
		float itemUp{ 1.0f };
		float handTurn{ 0.0f };      // degrees: the hand turned right (+) / left around the up axis
		float handTilt{ 8.0f };      // degrees: fingers tilted up (+) / down
		float handRoll{ -17.0f };    // degrees: palm rolled around the fingers
		float maxItemSize{ 20.0f };  // radius; bigger items are shown smaller
		// [Weapons] held by the grip: where the hand is (eye space) and how the blade leans, degrees
		float weaponRight{ 17.6f };
		float weaponForward{ 31.4f };
		float weaponUp{ -10.7f };
		float weaponLeanLeft{ 5.0f };      // blade tipped towards the left
		float weaponLeanForward{ 16.0f };  // blade tipped away from you
		float weaponRoll{ 12.0f };         // turned around the blade
		// [Bows] bows and crossbows: held further out (about arm's length) and more upright, so more of them is in view
		float bowRight{ 17.0f };
		float bowForward{ 30.4f };
		float bowUp{ -10.3f };
		float bowLeanLeft{ 18.0f };
		float bowLeanForward{ -7.0f };
		float raiseTime{ 0.45f };
		float stowTime{ 0.6f };
		float returnTime{ 0.5f };
	};

	// slider ranges, shared by the INI reader and the menu
	struct Range
	{
		float min;
		float max;
	};
	inline constexpr Range kSensitivity{ 0.05f, 5.0f };
	inline constexpr Range kHoldRight{ -20.0f, 30.0f };
	inline constexpr Range kHoldForward{ 8.0f, 40.0f };
	inline constexpr Range kHoldUp{ -35.0f, 10.0f };
	inline constexpr Range kMaxItemSize{ 2.0f, 60.0f };
	inline constexpr Range kHoldHeight{ 0.0f, 25.0f };
	inline constexpr Range kHandAngle{ -90.0f, 90.0f };
	inline constexpr Range kItemOffset{ -25.0f, 25.0f };
	inline constexpr Range kEffectSize{ 0.5f, 5.0f };
	inline constexpr Range kTime{ 0.05f, 3.0f };
	inline constexpr Range kFadeTime{ 0.05f, 1.0f };

	const Values& Get();
	Values&       Edit();  // the menu changes values in place, then calls Save()
	void          Load();
	void          Save();
}
