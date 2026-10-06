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
		bool itemSound{ true };         // the item's own pickup sound when it reaches the hand
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
		int   switchViewKey{ 33 };       // F: switches between 3rd and 1st person while holding (with the fade)
		int   switchViewGamepadKey{ 273 };  // RS (right stick click), like the game's own view switch
		bool  invertLookY{ false };
		float mouseSensitivity{ 1.0f };
		float gamepadSensitivity{ 1.0f };
		bool  invertY{ false };

		// [Hold] where the hand holds the item, in game units from the eye (right, forward, up)
		float holdRight{ 10.5f };
		float holdForward{ 31.4f };
		float holdUp{ -12.1f };
		float holdHeight{ 9.1f };    // how far the item floats above the palm
		float itemRight{ -2.0f };    // item offset from its place over the palm, eye space
		float itemForward{ -0.3f };
		float itemUp{ -4.7f };
		float handTurn{ 2.0f };      // degrees: the hand turned right (+) / left around the up axis
		float handTilt{ 14.0f };      // degrees: fingers tilted up (+) / down
		float handRoll{ -26.0f };    // degrees: palm rolled around the fingers
		float maxItemSize{ 20.0f };  // radius; bigger items are shown smaller
		// [Weapons] held by the grip: where the hand is (eye space) and how the blade leans, degrees
		float weaponRight{ 17.6f };
		float weaponForward{ 31.4f };
		float weaponUp{ -10.7f };
		float weaponLeanLeft{ 5.0f };      // blade tipped towards the left
		float weaponLeanForward{ 16.0f };  // blade tipped away from you
		float weaponRoll{ 12.0f };         // turned around the blade
		// [Bows] bows and crossbows: held further out (about arm's length) and more upright, so more of them is in view
		float bowRight{ 16.4f };
		float bowForward{ 30.4f };
		float bowUp{ -10.3f };
		float bowLeanLeft{ 18.0f };
		float bowLeanForward{ -7.0f };
		// [ThirdPerson] in 3rd person the camera flies in over the right shoulder instead of switching to 1st person.
		// Game units from the eyes in the direction the player faces (right, forward / back, up), scaled with the body.
		bool  thirdPerson{ true };
		float cameraRight{ 60.0f };
		float cameraBack{ 21.6f };
		float cameraUp{ 24.7f };
		float cameraTime{ 0.75f };  // seconds the camera flies in (and back)
		// seconds a pickup in 3rd person waits so Immersive Interactions' pickup animation plays first (only when that
		// mod is loaded; its "well timed" option off adds kLateAnimationWait)
		float animationWait{ 0.8f };
		bool  standStill{ true };  // the body's animation (idles too) settles and stops while the item is held
		float bodyItemScale{ 0.7f };  // items (not weapons) are shown this much smaller in 3rd person
		float bodyItemRight{ 7.2f };  // item offset from its place over the palm, as the camera sees it
		float bodyItemForward{ -0.3f };
		float bodyItemUp{ 9.1f };
		float bodyHoldRight{ 17.5f };  // where the hand holds the item, seen from the eyes
		float bodyHoldForward{ 22.2f };
		float bodyHoldUp{ -35.0f };
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
	inline constexpr Range kBodyHoldUp{ -50.0f, 10.0f };
	inline constexpr Range kItemScale{ 0.2f, 1.5f };
	inline constexpr Range kMaxItemSize{ 2.0f, 60.0f };
	inline constexpr Range kHoldHeight{ 0.0f, 25.0f };
	inline constexpr Range kHandAngle{ -90.0f, 90.0f };
	inline constexpr Range kItemOffset{ -25.0f, 25.0f };
	inline constexpr Range kEffectSize{ 0.5f, 5.0f };
	inline constexpr Range kTime{ 0.05f, 3.0f };
	inline constexpr Range kWait{ 0.0f, 3.0f };
	inline constexpr Range kFadeTime{ 0.05f, 1.0f };
	inline constexpr Range kCameraSide{ -90.0f, 90.0f };
	inline constexpr Range kCameraBack{ 0.0f, 150.0f };
	inline constexpr Range kCameraUp{ -40.0f, 60.0f };

	const Values& Get();
	Values&       Edit();  // the menu changes values in place, then calls Save()
	void          Load();
	void          Save();
}
