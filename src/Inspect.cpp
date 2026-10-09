#include "Inspect.h"

#include "ArmPose.h"
#include "BodyArm.h"
#include "ContainerLid.h"
#include "Fade.h"
#include "HUD.h"
#include "HeadLook.h"
#include "Keys.h"
#include "Lang.h"
#include "MathUtil.h"
#include "QuickLoot.h"
#include "Seen.h"
#include "Settings.h"
#include "SmoothCam.h"
#include "StoreDisplay.h"
#include "Telekinesis.h"
#include "WorldPause.h"

#include <atomic>

namespace Inspect
{
	using namespace MathUtil;
	using Flag = RE::UserEvents::USER_EVENT_FLAG;

	namespace
	{
		enum class Phase
		{
			kIdle,
			kWaitMenu,    // from the inventory: waiting for it to close
			kWaitAnimation,  // 3rd person: another mod's pickup animation (or the search of a container) plays first
			kFadeOut,     // from 3rd person: fading to black before the switch to 1st person
			kWaitCamera,  // switching to 1st person
			kRaise,       // hand brings the item up
			kHold,        // looking at it, mouse turns it
			kBuying,      // an item for sale (Purchaseable Store-Display-Items): its buy box is open, the hand keeps holding
			kSwitchView,  // switching between 3rd and 1st person while holding (fade to black, switch, fade back)
			kSettle,      // a floating item sinks onto the palm before it's put away / back
			kStow,        // hand goes down and behind the back (3rd person: into a pocket at the hip), item goes into the inventory
			kReturn,      // empty hand comes back to the normal pose
			kPutBack,     // item goes back where it was, hand lowers
			kFadeBack,    // back to 3rd person: fading to black before the switch
			kCameraBack   // staying in 3rd person: the camera flies back from over the shoulder
		};

		enum class Source
		{
			kWorld,      // taken from the world: picked up when it goes into the backpack
			kInventory,  // already ours, taken out of the inventory menu: the menu opens again at the end
			kContainer,  // QuickLoot's take from a container or body: taken out of it when it goes into the backpack
			kHarvest     // a plant's ingredient: the plant is harvested when it goes into the backpack
		};

		// a plant's Activate (TESFlora / TESObjectTREE), called when the harvest really happens
		using ActivateFunc = bool(RE::TESBoundObject*, RE::TESObjectREFR*, RE::TESObjectREFR*, std::uint8_t, RE::TESBoundObject*, std::int32_t);

		struct Harvest
		{
			RE::TESBoundObject* plant{ nullptr };
			ActivateFunc*       activate{ nullptr };
			std::uint8_t        arg3{ 0 };
			RE::TESBoundObject* object{ nullptr };
			std::int32_t        count{ 1 };
		};

		// wrist positions while putting the item away, eye space (right, forward, up): out of view, low and behind
		const NiPoint3 kStowVia{ 16.0f, 12.0f, -28.0f };
		const NiPoint3 kStowEnd{ 20.0f, -6.0f, -38.0f };
		// Staying in 3rd person, where the hand can be seen: the hand goes down to the right hip and slips the item into a
		// pocket, fingers first, then hangs back into its own animation. Eye space (right, forward, up), scaled with the
		// body; the pocket is about where the hanging hand is, so the arm stays natural all the way.
		const NiPoint3 kPocketLift{ -2.0f, -3.0f, 25.0f };  // the curve first pulls the hand up from the hold (it rises about 8)
		const NiPoint3 kPocketVia{ 22.0f, 12.0f, -46.0f };  // a little out to the side on the way down (past the thigh)
		const NiPoint3 kPocket{ 20.0f, 2.0f, -56.0f };      // the wrist at the right hip, the arm almost straight (elbow ~160 deg)
		const NiPoint3 kPocketFingers{ 0.05f, 0.1f, -1.0f };  // pointing down into the pocket, in line with the forearm
		const NiPoint3 kPocketPalm{ -1.0f, 0.0f, 0.0f };     // towards the thigh
		const NiPoint3 kPocketElbow{ 0.35f, -1.0f, 0.1f };   // the elbow goes back
		constexpr float kPocketShrinkFrom = 0.65f;          // part of the stow from which the item slips in (gone at kStowPickUpAt)
		constexpr std::array<float, 5> kPocketFist{ 0.9f, 1.15f, 1.2f, 1.25f, 1.3f };  // radians per joint, thumb first (ArmPose bends the thumb less)
		// sneaking: the body is crouched, so the pocket is found from the right thigh bone instead (eye space axes,
		// scaled), as tuned in game: out at the side of the crouched thigh, a little in front, lower down
		const NiPoint3 kPocketFromThigh{ 18.0f, 6.0f, -10.0f };
		// sneaking in 3rd person, as tuned in game: the hand held lower and further out to the right (eye space, from the
		// crouched head), the elbow bent more than the reach needs and swung in and forward, the wrist kept closer to its
		// animation (degrees)
		const NiPoint3  kSneakHold{ 34.2f, 24.9f, -54.0f };
		constexpr float kSneakElbowBend = 53.0f;
		constexpr float kSneakElbowOut = -47.0f;   // around the forward axis: + = out to the side
		constexpr float kSneakElbowBack = -20.0f;  // around the right axis: + = back
		constexpr float kSneakWristBend = 18.0f;
		// weapons (not bows) in 3rd person, as tuned in game: where the hand holds them next to other items (eye space,
		// scaled with the body) and the blade's angles (degrees: tipped left, tipped away, turned around the blade)
		const NiPoint3  kBodyWeaponOffset{ 0.0f, 1.4f, 0.0f };  // from the 3rd person hand position (fHand*)
		constexpr float kBodyWeaponLeanLeft = 5.0f;
		constexpr float kBodyWeaponLeanForward = 16.0f;
		constexpr float kBodyWeaponRoll = 12.0f;
		constexpr float kEyeAboveHead = 6.0f;   // 3rd person: the eyes above the head bone (which sits at the top of the neck)
		constexpr float kEyeForward = 5.0f;     // ... and in front of it
		constexpr float kEyeHeight = 125.0f;    // a skeleton without a head bone
		constexpr float kStowPickUpAt = 0.8f;  // part of the stow time after which the item is in the backpack
		constexpr float kCameraTimeout = 2.0f;
		constexpr float kCurl = 0.2f;          // finger bend while holding, radians per joint
		constexpr float kOpenCurl = 0.03f;     // telekinesis: open hand
		constexpr float kFloatBob = 0.5f;      // telekinesis: the item bobs this much (units) over the palm
		constexpr float kActivityRise = 6.0f;    // 1/s, how fast the hand's activity follows the turn speed up
		constexpr float kActivityFall = 1.5f;    // ... and settles when the turning stops
		constexpr float kLeanFollow = 3.0f;      // 1/s, wrist lean
		constexpr float kFingerFollow = 6.0f;    // 1/s, how fast each finger follows its target bend
		constexpr float kFistFollow = 14.0f;     // ... closing into a fist to put the item away (a quick grab)
		constexpr float kMaxFingerWave = 3.5f;   // radians per second the finger wave runs at most while the item turns
		constexpr float kSettleTime = 0.3f;      // seconds the item takes to sink onto the palm
		constexpr float kStealWake = 0.4f;       // stealing: seconds the woken world gets to see the player before the theft
		constexpr float kBuyTimeout = 30.0f;     // seconds (game running) without an answer from the store's buy script: not bought
		constexpr float kHeadFollow = 4.0f;      // 1/s: how fast the head turns to the item (3rd person)
		constexpr float kHeadReturn = 8.0f;      // ... and back to its animation when the item is put away
		constexpr float kCupCurl = 0.35f;        // fingers closing around an item lying on the palm
		constexpr float kRestAbovePalm = 3.0f;   // the item comes down near the palm, not all the way onto it
		constexpr float kGlowAbovePalm = 2.0f;   // the telekinesis hand effect's center over the palm
		constexpr float kLookReturn = 9.0f;      // 1/s: the head turns back after looking around (about a third of a second)
		constexpr float kLookSpeed = 0.0015f;    // radians per mouse unit when looking around
		constexpr float kMaxLookYaw = 0.52f;     // how far the head turns: about 30 degrees to each side
		constexpr float kMaxLookPitch = 0.6f;    // ... and about 35 degrees up / down (further it looks into the own arm)
		// weapons: held by the grip like an equipped weapon, the mouse turns the hand
		constexpr float kMaxTwist = 0.75f;       // radians around the blade (about 43 degrees each way)
		constexpr float kMaxBend = 0.5f;         // radians up / down (about 29 degrees)
		// Bows start with the string 45 degrees left of the eye, which already bends the wrist back about 45 degrees:
		// turning to the side bends it further (it broke past about 50 degrees more), turning towards the eye straightens
		// it first, then bends it the natural way.
		constexpr float kBowTwistToEye = 1.2f;   // about 70 degrees: past edge-on, the string comes round to the right
		constexpr float kBowTwistToSide = 0.7f;  // about 40 degrees: the side view, short of breaking the wrist
		constexpr float kTurnFollow = 14.0f;     // 1/s, the hand eases towards the turn the mouse asks for
		constexpr float kMaxFadeWait = 1.0f;     // a fade phase never waits longer than this
		constexpr float kViewSettle = 0.1f;   // seconds the camera gets after a switch before we place things in it
		constexpr float kIdleSettle = 0.35f;  // 3rd person: seconds the body blends back to standing before it holds still
		constexpr float kStillFollow = 3.0f;  // 1/s: how fast the body's animation slows down / speeds up again
		constexpr float kLateAnimationWait = 0.5f;  // Immersive Interactions picks up at the start of its animation, not halfway
		constexpr float kSearchStandUp = 0.6f;      // seconds after the searching animation for getting up, before the camera moves
		constexpr float kSearchTurn = 5.0f;         // 1/s: the player turns towards the container while searching
		constexpr float kRiseFromScale = 0.5f;      // an item out of a container comes up from this part of its size
		constexpr float kLootingStandUp = 0.2f;     // Dynamic Looting: seconds after its animation ended, before the camera moves
		constexpr float kMaxLootingWait = 6.0f;     // ... never waiting longer for it than this
		constexpr float kMouseSpeed = 0.006f;  // radians per mouse unit
		constexpr float kStickSpeed = 3.0f;    // radians per second at full stick
		constexpr float kWheelStep = 1.5f;     // units per wheel notch
		constexpr float kMaxCloser = 4.0f;     // mouse wheel range: closer than the hold position would clip the camera
		constexpr float kMaxFarther = 8.0f;
		constexpr float kInputDelay = 0.2f;    // seconds after the pickup before keys count
		constexpr float kDegrees = 3.14159265f / 180.0f;

		struct State
		{
			Phase                         phase{ Phase::kIdle };
			Source                        source{ Source::kWorld };
			float                         time{ 0.0f };   // in the current phase
			float                         clock{ 0.0f };  // since the start, for the idle sway
			float                         animationWait{ 0.0f };  // kWaitAnimation: seconds

			// the pickup we hold back
			RE::ObjectRefHandle           ref;         // from a container: the container
			RE::FormID                    baseID{ 0 };
			std::int32_t                  count{ 1 };
			bool                          arg3{ false };
			bool                          playSound{ true };
			bool                          pickedUp{ false };
			float                         awake{ -1.0f };  // stealing: seconds since the world woke up for the theft (-1 = not woken)
			// for sale (Purchaseable Store-Display-Items): taken for free, bought when it goes into the backpack
			bool                          forSale{ false };
			bool                          bought{ false };      // paid for and picked up by the store's script
			std::uint32_t                 buyRequest{ 0 };      // which buy the store's answer belongs to
			bool                          buyAnswered{ false };
			std::int32_t                  countBefore{ 0 };     // how many of the item the player had before buying
			ContainerItem                 take;           // from a container: what QuickLoot would have taken
			Harvest                       harvest;        // from a plant (the ref): its harvest, done when the item is stowed
			NiPoint3                      containerSpot;  // where the item comes out of the container / plant (and goes back in)
			bool                          lootingAnimation{ false };  // Dynamic Looting's harvest animation plays first
			bool                          riseFromHand{ false };      // ... after it the item comes up out of the right hand
			bool                          searching{ false };  // the searching animation plays
			float                         searchTime{ 0.0f };
			float                         searchHeading{ 0.0f };  // facing the container

			RE::NiPointer<RE::NiAVObject> worldModel;  // the reference's own 3D, hidden meanwhile
			RE::NiPointer<RE::NiAVObject> item;        // our copy in the hand
			NiTransform                   start;       // where the item lay (world)
			NiTransform                   last;        // where we drew it last (world)
			NiTransform                   inHand;      // relative to the hand while stowing
			bool                          inHandSet{ false };
			NiPoint3                      center;      // bound center, model space
			float                         scale{ 1.0f };
			float                         radius{ 1.0f };  // after scaling
			NiMatrix3                     rotation;    // item rotation in the camera frame
			float                         distanceOffset{ 0.0f };
			std::string                   name;

			// telekinesis look: fingers move while the item turns, the wrist leans into the turn
			bool                          telekinesis{ false };
			bool                          settled{ false };    // the item lies on the palm (no more floating)
			Phase                         afterSettle{ Phase::kStow };
			float                         activity{ 0.0f };  // 0..1, how fast the item is being turned (smoothed)
			float                         leanX{ 0.0f };     // smoothed turn speed around up / right, for the wrist lean
			float                         leanY{ 0.0f };
			float                         turnDirection{ 1.0f };  // which way the fingers "roll" the item

			// look key held: the mouse turns only the head (the view); the player, the hand and the item stay where they
			// are. Released, the head turns back. Offsets from the player's own facing, radians.
			bool                          looking{ false };
			float                         lookYaw{ 0.0f };    // + = right
			float                         lookPitch{ 0.0f };  // + = up
			bool                          headTurned{ false };     // we rotated the camera node last frame
			// weapons: held by the grip (hand bone space), fingers closed on the handle, turned by the mouse
			bool                          weapon{ false };
			bool                          staff{ false };  // staves: the shaft runs along the model's +Y, through the grip
			bool                          bow{ false };  // bows and crossbows: their own hold settings (held further out)
			bool                          gripSet{ false };
			NiTransform                   grip;
			NiTransform                   weaponNode;  // the WEAPON node (hand space, item scale): the grip before the fist fit
			NiPoint3                      bladeAxis;   // hand space
			bool                          stringBow{ false };  // bows (not crossbows): held up with the string towards the eye
			NiPoint3                      stringAxis;          // hand space: from the riser to the bowstring
			float                         twist{ 0.0f };
			float                         bend{ 0.0f };
			float                         twistTarget{ 0.0f };
			float                         bendTarget{ 0.0f };
			NiMatrix3                     holdHand;    // hand rotation in the last hold frame, start of the stow
			NiMatrix3                     cameraBase;              // its rotation before our turn
			NiMatrix3                     cameraWritten;           // what we wrote
			std::array<float, 5>          fingers{};         // current finger bend (smoothed)
			float                         fingerPhase{ 0.0f };  // runs faster while the item turns

			bool                          wasThirdPerson{ false };
			// staying in 3rd person: the 3rd person arm holds the item and the camera flies in over the right shoulder
			bool                          thirdPerson{ false };
			float                         bodyScale{ 1.0f };
			float                         eyeHeight{ kEyeHeight };  // above the player's root, measured at the start
			float                         eyeForward{ kEyeForward };  // in front of the root (unscaled); sneaking: measured at the start
			bool                          sneaking{ false };        // stays crouched: the pocket is found from the thigh
			float                         headWeight{ 0.0f };       // 3rd person: 0 = the head animates, 1 = it looks at the item
			NiPoint3                      lookTarget;               // the held item's middle (world), last placed
			bool                          lookTargetSet{ false };
			NiPoint3                      cameraStart;              // the rendered camera when we took it
			NiMatrix3                     cameraStartAxes;          // its forward, up, right
			float                         cameraBlend{ 0.0f };      // 0 = where the game had it, 1 = over the shoulder
			bool                          cameraIn{ false };        // where the blend is going
			float                         stillClock{ 0.0f };       // since the camera started flying in
			float                         stillness{ 0.0f };        // 0 = the body animates normally, 1 = it holds still
			bool                          switchRequested{ false };
			bool                          switchToThird{ false };   // kSwitchView: where to
			bool                          switched{ false };        // kSwitchView: the camera was switched (in the dark)
			bool                          captureStartOnReturn{ false };  // switched to 3rd person: fly back to where the game has the camera then
			bool                          movementOpened{ false };         // Improved Camera: movement "on" for its camera, keys blocked
			bool                          sneakKeyBlocked{ false };        // the sneak key does nothing (sneaking itself goes on)
			std::vector<Flag>             disabledControls;
			RE::NiPointer<RE::NiAVObject> hiddenWeapon;

			// input since the last update
			float mouseX{ 0.0f };
			float mouseY{ 0.0f };
			float stickX{ 0.0f };
			float stickY{ 0.0f };
			float wheel{ 0.0f };
			bool  storeRequested{ false };
			bool  putBackRequested{ false };
			bool  hudModePushed{ false };
		};
		State state;

		RE::PlayerCharacter* Player() { return RE::PlayerCharacter::GetSingleton(); }

		bool FirstPerson()
		{
			const auto camera = RE::PlayerCamera::GetSingleton();
			return camera && camera->IsInFirstPerson();
		}

		bool Active() { return state.phase != Phase::kIdle; }

		bool InventoryOpen()
		{
			const auto ui = RE::UI::GetSingleton();
			return ui && ui->IsMenuOpen(RE::InventoryMenu::MENU_NAME);
		}

		// the inventory or the Tab menu it may have been opened from (which keeps the game paused and blurred)
		bool InventoryMenusOpen()
		{
			const auto ui = RE::UI::GetSingleton();
			return ui && (ui->IsMenuOpen(RE::InventoryMenu::MENU_NAME) || ui->IsMenuOpen(RE::TweenMenu::MENU_NAME));
		}

		void ShowInventory(bool a_show)
		{
			if (const auto queue = RE::UIMessageQueue::GetSingleton()) {
				queue->AddMessage(RE::InventoryMenu::MENU_NAME, a_show ? RE::UI_MESSAGE_TYPE::kShow : RE::UI_MESSAGE_TYPE::kHide, nullptr);
			}
		}

		// Closes the inventory and the Tab menu. The Tab menu stays open behind an inventory opened from it and can come
		// back when the inventory closes, so this keeps checking for a while. Runs as UI tasks: those run while the game
		// is paused (our player update doesn't).
		constexpr int kCloseFrames = 60;

		void CloseInventoryMenus(int a_framesLeft = kCloseFrames)
		{
			const auto ui = RE::UI::GetSingleton();
			const auto queue = RE::UIMessageQueue::GetSingleton();
			if (!ui || !queue) {
				return;
			}
			for (const auto menu : { RE::InventoryMenu::MENU_NAME, RE::TweenMenu::MENU_NAME }) {
				if (ui->IsMenuOpen(menu)) {
					queue->AddMessage(menu, RE::UI_MESSAGE_TYPE::kHide, nullptr);
				}
			}
			if (a_framesLeft > 0) {
				SKSE::GetTaskInterface()->AddUITask([a_framesLeft]() {
					if (Active()) {
						CloseInventoryMenus(a_framesLeft - 1);
					}
				});
			}
		}

		// ---- original pickup ----

		struct PickUpObject
		{
			static void thunk(RE::PlayerCharacter* a_this, RE::TESObjectREFR* a_object, std::int32_t a_count, bool a_arg3, bool a_playSound);
			static inline REL::Relocation<decltype(thunk)> func;
		};

		// a_hidden: the reference's model we hid, shown again right before the pickup unloads it (no flash in the world)
		void PickUpNow(RE::ObjectRefHandle a_ref, std::int32_t a_count, bool a_arg3, bool a_playSound, RE::NiPointer<RE::NiAVObject> a_hidden)
		{
			SKSE::GetTaskInterface()->AddTask([a_ref, a_count, a_arg3, a_playSound, a_hidden]() {
				if (a_hidden) {
					a_hidden->SetAppCulled(false);
				}
				const auto player = Player();
				const auto ref = a_ref.get();
				if (player && ref && !ref->IsDisabled() && !ref->IsMarkedForDeletion()) {
					PickUpObject::func(player, ref.get(), a_count, a_arg3, a_playSound);
				}
			});
		}

		// ---- a plant's harvest, done when the item goes into the backpack ----

		bool Harvested(const RE::TESObjectREFR* a_plant)
		{
			return (a_plant->formFlags & RE::TESObjectREFR::RecordFlags::kHarvested) != 0;
		}

		// the plant's own Activate (ours is skipped): the ingredient (a leveled list rolls now), its sound and message,
		// the stealing, the harvested look
		void HarvestNow(RE::ObjectRefHandle a_plant, const Harvest& a_harvest)
		{
			SKSE::GetTaskInterface()->AddTask([a_plant, a_harvest]() {
				const auto player = Player();
				const auto ref = a_plant.get();
				if (player && ref && a_harvest.activate && !ref->IsDisabled() && !Harvested(ref.get())) {
					a_harvest.activate(a_harvest.plant, ref.get(), player, a_harvest.arg3, a_harvest.object, a_harvest.count);
				}
			});
		}

		// ---- QuickLoot's take, done when the item goes into the backpack ----

		bool HasExtraList(RE::TESObjectREFR* a_container, RE::TESBoundObject* a_object, RE::ExtraDataList* a_extraList)
		{
			const auto changes = a_extraList ? a_container->GetInventoryChanges() : nullptr;
			if (!changes || !changes->entryList) {
				return false;
			}
			for (const auto entry : *changes->entryList) {
				if (entry && entry->object == a_object && entry->extraLists) {
					for (const auto list : *entry->extraLists) {
						if (list == a_extraList) {
							return true;
						}
					}
				}
			}
			return false;
		}

		// the way QuickLoot takes a stack: the "added" event for quests and stats, the sound, the move into the
		// player's inventory, the alarm when stealing
		void GiveContainerItem(const ContainerItem& a_item)
		{
			const auto player = Player();
			const auto container = a_item.container.get();
			if (!player || !container || !a_item.object) {
				logs::warn("The container is gone: nothing taken");
				return;
			}
			const auto inventory = container->GetInventory([&](RE::TESBoundObject& a_object) { return &a_object == a_item.object; });
			const auto found = inventory.find(a_item.object);
			const std::int32_t count = found != inventory.end() ? std::min(a_item.count, found->second.first) : 0;
			if (count <= 0) {
				logs::warn("{:08X} isn't in the container any more: nothing taken", a_item.object->GetFormID());
				return;
			}
			// the stack's own list (enchantment, temper...) if the container still has it
			const auto extraList = HasExtraList(container.get(), a_item.object, a_item.extraList) ? a_item.extraList : nullptr;
			const auto owner = container->GetOwner();
			const auto actor = container->As<RE::Actor>();
			const auto type = a_item.stealing              ? RE::AQUIRE_TYPE::kSteal :
			                  actor && actor->IsDead(false) ? RE::AQUIRE_TYPE::kDeadBody :
			                                                  RE::AQUIRE_TYPE::kContainer;
			player->AddPlayerAddItemEvent(a_item.object, owner, container.get(), type);
			player->PlayPickUpSound(a_item.object, true, false);
			container->RemoveItem(a_item.object, count, a_item.stealing ? RE::ITEM_REMOVE_REASON::kSteal : RE::ITEM_REMOVE_REASON::kStoreInContainer,
				extraList, player);
			a_item.object->HandleRemoveItemFromContainer(container.get());
			if (actor && a_item.object->IsAmmo()) {
				actor->ClearExtraArrows();
			}
			if (a_item.stealing) {
				player->StealAlarm(container.get(), a_item.object, count, a_item.value, owner, true);
			}
			logs::info("Took {:08X} x{} out of {:08X}{}", a_item.object->GetFormID(), count, container->GetFormID(), a_item.stealing ? " (stolen)" : "");
		}

		// ---- hint ----

		void ShowHint()
		{
			const auto& settings = Settings::Get();
			if (!settings.showHint) {
				return;
			}
			const auto input = RE::BSInputDeviceManager::GetSingleton();
			const bool gamepad = input && input->IsGamepadEnabled();
			const auto store = Keys::Name(gamepad ? settings.storeGamepadKey : settings.storeKey);
			const auto putBack = Keys::Name(gamepad ? settings.putBackGamepadKey : settings.putBackKey);
			std::string text = state.name.empty() ? "" : state.name + "    ";
			const char* storeText = Lang::T(state.forSale ? "Buy" : "Put in backpack");
			text += state.source != Source::kInventory ? std::format("[{}] {}    [{}] {}", store, storeText, putBack, Lang::T("Put back")) :
			                                         std::format("[{}] {}", store, storeText);
			const auto switchView = Keys::Name(gamepad ? settings.switchViewGamepadKey : settings.switchViewKey);
			text += std::format("    [{}] {}", switchView, Lang::T("Switch view"));
			HUD::ShowHint(text);  // stays until the item is put away
		}

		// ---- controls ----

		void BlockMovementKeys(bool a_block);
		void MovementForCamera();

		void DisableControls()
		{
			const auto controls = RE::ControlMap::GetSingleton();
			if (!controls) {
				return;
			}
			// Not the sneaking controls: switching those off makes the player stand up (the player's UserEventEnabledEvent
			// sink ends sneaking, AE 40739). Only the sneak key is blocked, so a sneaking player stays crouched.
			for (const auto flag : { Flag::kMovement, Flag::kLooking, Flag::kActivate, Flag::kMenu, Flag::kPOVSwitch, Flag::kFighting,
					 Flag::kWheelZoom, Flag::kJumping }) {
				if (controls->AreControlsEnabled(flag)) {
					controls->ToggleControls(flag, false, false);
					state.disabledControls.push_back(flag);
				}
			}
			if (const auto playerControls = RE::PlayerControls::GetSingleton(); playerControls && playerControls->sneakHandler && !state.sneakKeyBlocked) {
				playerControls->sneakHandler->SetInputEventHandlingEnabled(false);
				state.sneakKeyBlocked = true;
			}
			if (!state.hudModePushed) {
				HUD::SetHoldMode(true);  // no crosshair item info, compass or bars while holding
				state.hudModePushed = true;
			}
		}

		void RestoreControls()
		{
			if (state.movementOpened) {
				BlockMovementKeys(false);
				state.movementOpened = false;
			}
			if (state.sneakKeyBlocked) {
				if (const auto playerControls = RE::PlayerControls::GetSingleton(); playerControls && playerControls->sneakHandler) {
					playerControls->sneakHandler->SetInputEventHandlingEnabled(true);
				}
				state.sneakKeyBlocked = false;
			}
			if (const auto controls = RE::ControlMap::GetSingleton()) {
				for (const auto flag : state.disabledControls) {
					controls->ToggleControls(flag, true, false);
				}
			}
			state.disabledControls.clear();
			if (state.hudModePushed) {
				HUD::SetHoldMode(false);
				state.hudModePushed = false;
			}
			HUD::HideHint();
		}

		// ---- the item copy ----

		void DetachItem()
		{
			if (state.item && state.item->parent) {
				state.item->parent->DetachChild(state.item.get());
			}
		}

		void ShowWorldModel()
		{
			if (state.worldModel) {
				state.worldModel->SetAppCulled(false);
				state.worldModel.reset();
			}
		}

		// a_keepScale: the model is shown at its real size (world reference), else at scale 1 (inventory preview, which is scaled to fit)
		bool MakeItem(RE::NiAVObject* a_model, bool a_keepScale, bool a_limitSize = true)
		{
			const auto copy = a_model->Clone();
			const auto node = copy ? netimmerse_cast<RE::NiAVObject*>(copy) : nullptr;
			if (!node) {
				return false;
			}
			state.item.reset(node);
			// our copy must not collide with anything
			RE::BSVisit::TraverseScenegraphObjects(node, [](RE::NiAVObject* a_object) {
				a_object->collisionObject.reset();
				return RE::BSVisit::BSVisitControl::kContinue;
			});
			node->SetAppCulled(false);

			state.start = a_model->world;
			const float worldScale = a_model->world.scale > 0.0f ? a_model->world.scale : 1.0f;
			const float realScale = a_keepScale ? worldScale : 1.0f;
			const float radius = std::max(a_model->worldBound.radius / worldScale * realScale, 1.0f);
			state.center = a_model->world.rotate.Transpose() * (a_model->worldBound.center - a_model->world.translate) / worldScale;
			const float maxSize = a_limitSize ? Settings::Get().maxItemSize : radius;
			state.scale = realScale * std::min(1.0f, maxSize / radius);
			state.radius = std::min(radius, maxSize);
			return true;
		}

		// weapons are held by the grip; bows and crossbows have their own hold, bows like an archer
		void SetWeaponKind(const RE::TESForm* a_object)
		{
			state.weapon = a_object->IsWeapon();
			if (const auto weapon = a_object->As<RE::TESObjectWEAP>()) {
				state.bow = weapon->IsBow() || weapon->IsCrossbow();
				state.stringBow = weapon->IsBow();
				state.staff = weapon->IsStaff();
			}
		}

		std::string ItemName(const RE::TESForm* a_object)
		{
			const auto named = a_object ? a_object->As<RE::TESFullName>() : nullptr;
			return named && named->GetFullName() ? named->GetFullName() : "";
		}

		// the item turned and scaled, with its middle (the model's bound center) at a point
		NiTransform ItemAt(const NiPoint3& a_middle, const NiMatrix3& a_rotate, float a_scale)
		{
			NiTransform world;
			world.rotate = a_rotate;
			world.scale = a_scale;
			world.translate = a_middle - a_rotate * (state.center * a_scale);
			return world;
		}

		RE::NiNode* FirstPersonRoot()
		{
			const auto player = Player();
			const auto root = player ? player->Get3D(true) : nullptr;
			return root ? root->AsNode() : nullptr;
		}

		RE::NiNode* BodyRoot()
		{
			const auto player = Player();
			const auto root = player ? player->Get3D(false) : nullptr;
			return root ? root->AsNode() : nullptr;
		}

		// the skeleton whose arm holds the item
		RE::NiNode* ArmRoot() { return state.thirdPerson ? BodyRoot() : FirstPersonRoot(); }

		void PlaceItem(const NiTransform& a_world)
		{
			const auto root = ArmRoot();
			if (!state.item || !root) {
				return;
			}
			if (state.item->parent != root) {
				DetachItem();
				root->AttachChild(state.item.get(), true);
			}
			state.item->local = ToLocal(root->world, a_world);
			RE::NiUpdateData update{};
			state.item->Update(update);
			state.last = a_world;
			state.lookTarget = a_world.translate + a_world.rotate * (state.center * a_world.scale);
			state.lookTargetSet = true;
		}

		void HideWeapon()
		{
			const auto player = Player();
			const auto root = ArmRoot();
			if (!root || !player->AsActorState()->IsWeaponDrawn()) {
				return;
			}
			if (const auto weapon = root->GetObjectByName("WEAPON"); weapon && !weapon->GetAppCulled()) {
				weapon->SetAppCulled(true);
				state.hiddenWeapon.reset(weapon);
			}
		}

		// ---- flow ----

		void SetPhase(Phase a_phase)
		{
			logs::debug("Phase {} -> {}", static_cast<int>(state.phase), static_cast<int>(a_phase));
			state.phase = a_phase;
			state.time = 0.0f;
		}

		// Ends a look-around: a_snap = the head is straight again right away
		void StopLooking(bool a_snap);

		// whether putting the held item in the backpack is a crime
		bool Stealing()
		{
			switch (state.source) {
			case Source::kWorld:
			case Source::kHarvest:
				{
					const auto ref = state.ref.get();
					return ref && ref->IsCrimeToActivate();
				}
			case Source::kContainer:
				return state.take.stealing;
			default:
				return false;
			}
		}

		// The theft check (the game's pickup / harvest, our StealAlarm for containers) only finds witnesses while the
		// AI and detection run, so a paused world wakes up as soon as a stolen item is to be put away
		void WakeWorldForTheft()
		{
			if (state.awake < 0.0f && WorldPause::Active() && Stealing()) {
				WorldPause::End();
				state.awake = 0.0f;
				logs::info("Stealing: the world wakes up to see it");
			}
		}

		// the item may go into the backpack now (a woken world had time to look at the player)
		bool TheftSeen()
		{
			return state.awake < 0.0f || state.awake >= kStealWake;
		}

		// Improved Camera 2 hides the 1st person arms its own way (the 1.x arm scale is handled by ArmPose) and shows
		// them while the player's behavior variable bOffsetGPMA (Offset Movement Animation) is true: it's kept true
		// while our hand holds the item in 1st person, then set back. Without that variable this does nothing.
		bool cameraArmsShown{ false };
		bool cameraArmsBefore{ false };

		void ShowCameraArms(bool a_show)
		{
			static const RE::BSFixedString variable{ "bOffsetGPMA" };
			const auto player = Player();
			if (!player) {
				return;
			}
			if (!a_show) {
				if (cameraArmsShown) {
					player->SetGraphVariableBool(variable, cameraArmsBefore);
					cameraArmsShown = false;
				}
				return;
			}
			if (!cameraArmsShown) {
				bool before = false;
				const bool found = player->GetGraphVariableBool(variable, before);
				cameraArmsBefore = found && before;
				cameraArmsShown = true;
				static bool logged = false;
				if (!logged) {
					logged = true;
					if (found) {
						logs::info("bOffsetGPMA found in the player's behavior: kept on while holding, so Improved Camera 2 shows the arms");
					} else {
						logs::info("No bOffsetGPMA in the player's behavior (no Offset Movement Animation): nothing to tell Improved Camera 2");
					}
				}
			}
			player->SetGraphVariableBool(variable, true);  // every frame, as Improved Camera asks for
		}

		// game load: the behavior graph is new
		void ForgetCameraArms()
		{
			cameraArmsShown = false;
		}

		void Finish()
		{
			ShowCameraArms(false);
			if (state.searching) {
				Player()->NotifyAnimationGraph("IdleForceDefaultState");  // cut short: up from the search
			}
			StopLooking(true);
			WorldPause::End();
			Telekinesis::Stop();
			ArmPose::Release();
			HeadLook::Release();
			BodyArm::Restore();
			DetachItem();
			ShowWorldModel();
			RestoreControls();
			if (state.hiddenWeapon) {
				state.hiddenWeapon->SetAppCulled(false);
			}
			SmoothCam::GiveBack();  // also after a switch to 1st person: we keep it until the end
			if (state.wasThirdPerson) {
				if (const auto camera = RE::PlayerCamera::GetSingleton(); camera && camera->IsInFirstPerson()) {
					camera->ForceThirdPerson();
				}
			}
			Fade::To(0.0f, Settings::Get().fadeTime * 1.5f);  // after a fade to black: show the world again
			const bool reopen = state.source == Source::kInventory && state.phase != Phase::kWaitMenu;
			const bool container = state.source == Source::kContainer;
			const auto lid = state.ref;
			state = {};
			if (reopen) {
				ShowInventory(true);
			}
			if (container) {
				ContainerLid::Release(lid);  // already closed when the item went in / out, unless it ended early
				QuickLoot::Pause(false);  // its menu comes back, without the item if it was taken
			}
		}

		bool MenuOpen()
		{
			const auto ui = RE::UI::GetSingleton();
			if (!ui || ui->GameIsPaused()) {
				return true;
			}
			for (const auto menu : { RE::ContainerMenu::MENU_NAME, RE::BookMenu::MENU_NAME, RE::InventoryMenu::MENU_NAME,
					 RE::BarterMenu::MENU_NAME, RE::GiftMenu::MENU_NAME, RE::DialogueMenu::MENU_NAME, "LootMenu"sv }) {
				if (ui->IsMenuOpen(menu)) {
					return true;
				}
			}
			return false;
		}

		// the player can hold something in front of the face right now
		bool PlayerFree(RE::PlayerCharacter* a_player)
		{
			const auto actorState = a_player->AsActorState();
			return !a_player->IsDead() && !a_player->IsOnMount() && !a_player->IsInKillMove() && !actorState->IsSwimming() &&
			       actorState->GetSitSleepState() == RE::SIT_SLEEP_STATE::kNormal && actorState->GetKnockState() == RE::KNOCK_STATE_ENUM::kNormal &&
			       a_player->Get3D(true);
		}

		// "Hold a key to inspect": whether the key is held now, and whether it was held when Activate was last pressed.
		// Pickups and harvests can come a second after the press (Immersive Interactions / Dynamic Looting play their
		// animation first), so the press decides; the key held right now also counts (QuickLoot on another key).
		std::atomic<bool> inspectKeyHeld{ false };
		std::atomic<bool> activatedWithKey{ false };

		bool InspectKeyAllows()
		{
			return !Settings::Get().holdKeyToInspect || inspectKeyHeld || activatedWithKey;
		}

		// the kinds of items that go to the hand (the settings can send some straight to the inventory)
		bool KindInspected(const RE::TESForm* a_base)
		{
			const auto& settings = Settings::Get();
			switch (a_base->GetFormType()) {
			case RE::FormType::Weapon:
				return !settings.skipWeapons;
			case RE::FormType::Armor:
				return !settings.skipArmor;
			case RE::FormType::Misc:
			case RE::FormType::Ingredient:
			case RE::FormType::AlchemyItem:
			case RE::FormType::SoulGem:
			case RE::FormType::KeyMaster:
			case RE::FormType::Scroll:
			case RE::FormType::Light:
				return !(settings.skipGold && a_base->IsGold());
			case RE::FormType::Ammo:
				return !settings.skipAmmo;
			default:
				return false;  // books open their menu first, the rest isn't carried
			}
		}

		// a_forSale: an item for sale (Purchaseable Store-Display-Items), bought when it goes into the backpack: no theft
		bool CanInspect(RE::PlayerCharacter* a_player, RE::TESObjectREFR* a_ref, bool a_forSale = false)
		{
			const auto& settings = Settings::Get();
			if (!settings.enabled || Active() || !InspectKeyAllows() || !a_ref || a_ref->IsDisabled() || a_ref->IsMarkedForDeletion() || !a_ref->Get3D()) {
				return false;
			}
			const auto base = a_ref->GetBaseObject();
			if (!base || !KindInspected(base)) {
				return false;
			}
			if (!settings.alwaysInspect && Seen::Contains(base->GetFormID())) {
				return false;  // looked at this kind of item before
			}
			if (!a_forSale && !settings.inspectStolen && a_ref->IsCrimeToActivate()) {
				return false;  // stealing: normal pickup
			}
			if (!PlayerFree(a_player) || (settings.skipInCombat && a_player->IsInCombat()) || MenuOpen()) {
				return false;
			}
			return true;
		}

		// ---- staying in 3rd person: the camera over the shoulder ----

		bool ThirdPersonCamera()
		{
			const auto camera = RE::PlayerCamera::GetSingleton();
			return camera && camera->currentState && camera->currentState->id == RE::CameraState::kThirdPerson;
		}

		// the camera is still in the view the inspection runs in
		bool CameraStillOurs() { return state.thirdPerson ? ThirdPersonCamera() : FirstPerson(); }

		// The rendered camera's axes as columns forward, up, right. Gamebryo cameras look along their +X with +Y up.
		NiMatrix3 CameraAxes(const NiMatrix3& a_rotate)
		{
			const NiPoint3 forward = Column(a_rotate, 0);
			const NiPoint3 up = Column(a_rotate, 1);
			return FromColumns(forward, up, forward.Cross(up));
		}

		NiMatrix3 CameraAxes(const CameraFrame& a_frame) { return FromColumns(a_frame.forward, a_frame.up, a_frame.forward.Cross(a_frame.up)); }

		// The rendered camera relative to the camera root it hangs from, from the nodes' own (local) transforms: their
		// world transforms can still be last frame's when the camera state just changed (after a switch from 1st person
		// the camera ended up near the hands)
		std::optional<NiTransform> CameraInRoot(const RE::NiAVObject* a_root, const RE::NiAVObject* a_camera)
		{
			NiTransform relative;
			auto        node = a_camera;
			for (int depth = 0; node && node != a_root; node = node->parent, ++depth) {
				if (depth == 8) {
					return std::nullopt;
				}
				relative = Combine(node->local, relative);
			}
			return node ? std::optional(relative) : std::nullopt;
		}

		// where the game put the rendered camera this frame (its camera root's local, just set by the camera state)
		std::optional<NiTransform> GameCamera()
		{
			const auto playerCamera = RE::PlayerCamera::GetSingleton();
			const auto root = playerCamera ? playerCamera->cameraRoot.get() : nullptr;
			const auto camera = RE::Main::WorldRootCamera();
			const auto relative = root && camera ? CameraInRoot(root, camera) : std::nullopt;
			if (!relative) {
				return std::nullopt;
			}
			const NiTransform rootWorld = root->parent ? Combine(root->parent->world, root->local) : root->local;
			return Combine(rootWorld, *relative);
		}

		// Puts the rendered camera at a position with the given axes, by moving the camera root it hangs from (the game
		// sets that root every frame, so this runs every frame after the camera state's update)
		void PlaceCamera(const NiPoint3& a_position, const NiMatrix3& a_axes)
		{
			const auto playerCamera = RE::PlayerCamera::GetSingleton();
			const auto root = playerCamera ? playerCamera->cameraRoot.get() : nullptr;
			const auto camera = RE::Main::WorldRootCamera();
			const auto relative = root && camera ? CameraInRoot(root, camera) : std::nullopt;
			if (!relative) {
				return;
			}
			// the camera's world rotation is its axes (forward, up, right as columns), its root's is that without the
			// camera's own turn below the root
			NiTransform world;
			world.rotate = a_axes * relative->rotate.Transpose();
			world.scale = root->parent ? root->parent->world.scale * root->local.scale : root->local.scale;
			world.translate = a_position - world.rotate * relative->translate * world.scale;
			root->local = root->parent ? ToLocal(root->parent->world, world) : world;
			RE::NiUpdateData update{};
			root->Update(update);
		}

		// 3rd person frames: body = the eyes, facing like the player (where the hand is held from); view = the camera
		// over the right shoulder, looking at the hand (what the item turns in)
		struct BodyFrames
		{
			CameraFrame body;
			CameraFrame view;
		};

		// 3rd person: where the hand holds the item, eye space
		NiPoint3 BodyHold()
		{
			const auto& settings = Settings::Get();
			return state.sneaking ? kSneakHold : NiPoint3{ settings.bodyHoldRight, settings.bodyHoldForward, settings.bodyHoldUp };
		}

		std::optional<BodyFrames> ThirdPersonFrames()
		{
			const auto player = Player();
			const auto root = BodyRoot();
			if (!player || !root) {
				return std::nullopt;
			}
			const auto& settings = Settings::Get();
			const float scale = state.bodyScale;
			BodyFrames  frames;
			frames.body = MakeCameraFrame(root->world.translate + NiPoint3{ 0.0f, 0.0f, state.eyeHeight }, 0.0f, player->data.angle.z);
			frames.body.eye += frames.body.forward * (state.eyeForward * scale);
			// the camera: over the shoulder like standing, looking where a standing hand would hold the item. Sneaking it
			// only comes down with the crouched head (not forward with it), so the character stays in view.
			CameraFrame standing = frames.body;
			if (state.sneaking) {
				standing = MakeCameraFrame(root->world.translate + NiPoint3{ 0.0f, 0.0f, state.eyeHeight }, 0.0f, player->data.angle.z);
				standing.eye += standing.forward * (kEyeForward * scale);
			}
			const NiPoint3 camera = standing.ToWorld(NiPoint3{ settings.cameraRight, -settings.cameraBack, settings.cameraUp } * scale);
			const NiPoint3 hand = standing.ToWorld(NiPoint3{ settings.bodyHoldRight, settings.bodyHoldForward, settings.bodyHoldUp } * scale);
			const NiPoint3 look = hand + standing.up * (settings.holdHeight * scale);  // the item floats there
			const NiPoint3 direction = Normalized(look - camera, standing.forward);
			frames.view = MakeCameraFrame(camera, std::asin(std::clamp(-direction.z, -1.0f, 1.0f)), std::atan2(direction.x, direction.y));
			return frames;
		}

		// Stays in 3rd person: measures the body for the frames and takes the camera (from SmoothCam too)
		bool StartThirdPerson()
		{
			const auto root = BodyRoot();
			const auto camera = RE::Main::WorldRootCamera();
			if (!root || !camera) {
				return false;
			}
			state.bodyScale = root->world.scale > 0.0f ? root->world.scale : 1.0f;
			const auto head = root->GetObjectByName("NPC Head [Head]");
			state.eyeHeight = head ? head->world.translate.z - root->world.translate.z + kEyeAboveHead * state.bodyScale : kEyeHeight * state.bodyScale;
			// sneaking, the body leans forward and the head is well in front of the feet: the hand is held from there
			state.sneaking = Player()->IsSneaking();
			state.eyeForward = kEyeForward;
			if (state.sneaking && head) {
				const auto facing = MakeCameraFrame(root->world.translate, 0.0f, Player()->data.angle.z);
				state.eyeForward += (head->world.translate - root->world.translate).Dot(facing.forward) / state.bodyScale;
			}
			const auto start = GameCamera();
			state.cameraStart = start ? start->translate : camera->world.translate;
			state.cameraStartAxes = CameraAxes(start ? start->rotate : camera->world.rotate);
			state.cameraBlend = 0.0f;
			state.cameraIn = true;
			state.thirdPerson = true;
			state.stillClock = 0.0f;
			SmoothCam::TakeCamera();
			// idles (modded ones too) end: back to standing. Not while sneaking or with a weapon out, which that would undo.
			if (const auto player = Player();
				Settings::Get().standStill && !player->IsSneaking() && !player->AsActorState()->IsWeaponDrawn()) {
				player->NotifyAnimationGraph("IdleForceDefaultState");
			}
			logs::info("Staying in 3rd person (eyes {:.1f} above the feet, {:.1f} in front{}, body scale {:.2f})", state.eyeHeight,
				state.eyeForward * state.bodyScale, state.sneaking ? ", sneaking" : "", state.bodyScale);
			return true;
		}

		// to 1st person (or the camera over the shoulder) and no walking / looking around while the hand holds the item
		void TakeControl()
		{
			const auto& settings = Settings::Get();
			DisableControls();
			if (settings.worldWaits) {
				WorldPause::Begin();
			}
			const auto camera = RE::PlayerCamera::GetSingleton();
			if (!camera->IsInFirstPerson()) {
				if (settings.thirdPerson && ThirdPersonCamera() && StartThirdPerson()) {
					SetPhase(Phase::kWaitCamera);
					ShowHint();
					return;
				}
				state.wasThirdPerson = true;
			}
			MovementForCamera();  // 1st person from here on
			if (state.wasThirdPerson) {
				if (settings.fadeTransition) {
					// a short fade to black hides the camera jump
					Fade::To(1.0f, settings.fadeTime);
					SetPhase(Phase::kFadeOut);
					ShowHint();
					return;
				}
				camera->ForceFirstPerson();
			}
			SetPhase(Phase::kWaitCamera);
			ShowHint();
		}

		// the end of an inspection: from 3rd person fade to black first, the switch back happens in the dark
		void End()
		{
			const auto& settings = Settings::Get();
			if (state.thirdPerson) {
				// the hand is done: it goes back to its animation while the camera flies back, then it's over
				Telekinesis::Stop();
				ArmPose::Release();
				DetachItem();
				ShowWorldModel();
				state.cameraIn = false;
				SetPhase(Phase::kCameraBack);
				return;
			}
			if (state.wasThirdPerson && settings.fadeTransition) {
				Fade::To(1.0f, settings.fadeTime);
				SetPhase(Phase::kFadeBack);
				return;
			}
			Finish();
		}

		// ---- looking around: only the head turns ----

		void StartLooking()
		{
			state.looking = true;
		}

		// a_snap: the head is straight again right away, else it turns back smoothly (UpdateLookReturn)
		void StopLooking(bool a_snap)
		{
			state.looking = false;
			if (a_snap) {
				state.lookYaw = state.lookPitch = 0.0f;
			}
		}

		// look key held: mouse / right stick turn the head
		void ApplyLookInput(float a_delta)
		{
			const auto& settings = Settings::Get();
			const float invert = settings.invertLookY ? -1.0f : 1.0f;
			state.lookYaw += state.mouseX * kLookSpeed * settings.mouseSensitivity + state.stickX * kStickSpeed * 0.5f * a_delta * settings.gamepadSensitivity;
			// mouse / stick up looks up
			state.lookPitch -= (state.mouseY * kLookSpeed * settings.mouseSensitivity - state.stickY * kStickSpeed * 0.5f * a_delta * settings.gamepadSensitivity) * invert;
			state.lookYaw = std::clamp(state.lookYaw, -kMaxLookYaw, kMaxLookYaw);
			state.lookPitch = std::clamp(state.lookPitch, -kMaxLookPitch, kMaxLookPitch);
		}

		// let go: the head eases back to looking straight ahead
		void UpdateLookReturn(float a_delta)
		{
			if (state.looking) {
				return;
			}
			const float follow = std::min(1.0f, a_delta * kLookReturn);
			state.lookYaw -= state.lookYaw * follow;
			state.lookPitch -= state.lookPitch * follow;
			if (std::abs(state.lookYaw) < 1e-3f && std::abs(state.lookPitch) < 1e-3f) {
				state.lookYaw = state.lookPitch = 0.0f;
			}
		}

		// Turns the rendered view (the camera node) by the head's look offset, after the hand and the item were placed
		// from the player's own facing: the player doesn't turn, so the hand and the item stay where they are in the world.
		// The game sets the camera node every frame; if it ever didn't, our last turn is taken off first, never stacked.
		void ApplyHeadLook(const CameraFrame& a_frame)
		{
			const auto camera = RE::PlayerCamera::GetSingleton();
			const auto node = camera ? camera->cameraRoot.get() : nullptr;
			if (!node) {
				return;
			}
			const NiMatrix3 current = node->local.rotate;
			const NiMatrix3 base = state.headTurned && SameRotation(current, state.cameraWritten) ? state.cameraBase : current;
			if (state.lookYaw == 0.0f && state.lookPitch == 0.0f) {
				if (state.headTurned) {
					node->local.rotate = base;
					RE::NiUpdateData update{};
					node->Update(update);
					state.headTurned = false;
				}
				return;
			}
			const NiMatrix3 parent = node->parent ? node->parent->world.rotate : NiMatrix3{};
			// turn right around the world's up axis, look up around the view's right axis
			const NiMatrix3 turn = AxisAngle({ 0.0f, 0.0f, 1.0f }, -state.lookYaw) * AxisAngle(a_frame.right, state.lookPitch);
			node->local.rotate = parent.Transpose() * (turn * (parent * base));
			RE::NiUpdateData update{};
			node->Update(update);
			state.cameraBase = base;
			state.cameraWritten = node->local.rotate;
			state.headTurned = true;
		}

		RE::NiAVObject* HandMagicNode();

		// Improved Camera SE takes disabled movement for a scripted scene: 1.x entering 3rd person after 1st person keeps
		// a "fake 1st person" camera at the head (over ours: the camera sat at the hands), 2 switches 1st person to 3rd
		bool ImprovedCamera()
		{
			static const bool loaded = [] {
				const bool found = REX::W32::GetModuleHandleW(L"ImprovedCameraSE.dll") != nullptr;
				if (found) {
					logs::info("Improved Camera SE is loaded: in 1st person and when switching to 3rd person the movement controls stay on for it (the keys are blocked)");
				}
				return found;
			}();
			return loaded;
		}

		// the keys that move the player while the movement controls count as enabled
		void BlockMovementKeys(bool a_block)
		{
			const auto playerControls = RE::PlayerControls::GetSingleton();
			if (!playerControls) {
				return;
			}
			for (const auto handler : { static_cast<RE::PlayerInputHandler*>(playerControls->movementHandler),
					 static_cast<RE::PlayerInputHandler*>(playerControls->autoMoveHandler),
					 static_cast<RE::PlayerInputHandler*>(playerControls->sprintHandler) }) {
				if (handler) {
					handler->SetInputEventHandlingEnabled(!a_block);
				}
			}
		}

		// Movement counts as enabled again (what Improved Camera checks) while the movement keys stay blocked
		void OpenMovementForCamera(bool a_open)
		{
			const auto controls = RE::ControlMap::GetSingleton();
			if (!controls || a_open == state.movementOpened ||
				std::ranges::find(state.disabledControls, Flag::kMovement) == state.disabledControls.end()) {
				return;
			}
			BlockMovementKeys(a_open);
			controls->ToggleControls(Flag::kMovement, a_open, false);
			state.movementOpened = a_open;
		}

		// Improved Camera 2 takes disabled movement for a scripted scene: from 1st person it switches to 3rd (its
		// animation camera), which ended the inspection right away. In 1st person movement stays "on" for it.
		void MovementForCamera()
		{
			if (ImprovedCamera()) {
				OpenMovementForCamera(!state.thirdPerson);
			}
		}

		// ---- switching the view while holding: fade to black, switch 3rd <-> 1st person, fade back ----

		void BeginSwitchView()
		{
			const auto& settings = Settings::Get();
			StopLooking(true);
			state.switchToThird = !state.thirdPerson;
			state.switched = false;
			if (settings.fadeTransition) {
				Fade::To(1.0f, settings.fadeTime);
			}
			SetPhase(Phase::kSwitchView);
		}

		// in the dark: the hand lets go of the old view's arm and the camera switches; the inspection ends in this view
		void SwitchViewNow()
		{
			ShowCameraArms(false);
			Telekinesis::Stop();
			ArmPose::Release();
			HeadLook::Release();
			state.headWeight = 0.0f;
			BodyArm::Restore();
			if (state.hiddenWeapon) {
				state.hiddenWeapon->SetAppCulled(false);
				state.hiddenWeapon.reset();
			}
			state.headTurned = false;
			state.gripSet = false;  // the other skeleton's hand holds the weapon now
			state.wasThirdPerson = false;
			const auto camera = RE::PlayerCamera::GetSingleton();
			if (state.switchToThird) {
				if (ImprovedCamera()) {
					// it must see a normal 3rd person (no idle playing, movement on) to stop treating us as in 1st person
					if (const auto player = Player(); !player->IsSneaking() && !player->AsActorState()->IsWeaponDrawn()) {
						player->NotifyAnimationGraph("IdleForceDefaultState");
					}
					OpenMovementForCamera(true);
				}
				camera->ForceThirdPerson();
			} else {
				// SmoothCam's camera stays ours until the end: given back and asked for again, it was refused
				state.thirdPerson = false;
				state.stillness = 0.0f;
				MovementForCamera();
				camera->ForceFirstPerson();
			}
			state.switched = true;
			state.time = 0.0f;
			logs::info("Switched to {} person while holding", state.switchToThird ? "3rd" : "1st");
		}

		// the new view is there: the camera over the shoulder right away (it's still dark), the hand effect on the
		// new hand, then the screen clears
		void EndSwitchView()
		{
			const auto& settings = Settings::Get();
			if (state.switchToThird) {
				OpenMovementForCamera(false);
			}
			if (state.switchToThird && ThirdPersonCamera() && StartThirdPerson()) {
				state.cameraBlend = 1.0f;
				state.captureStartOnReturn = true;  // the game's camera may still be zooming out from 1st person now
			}
			HideWeapon();
			if (state.telekinesis) {
				Telekinesis::Start(HandMagicNode(), { .size = settings.telekinesisSize, .light = settings.telekinesisLight });
			}
			if (settings.fadeTransition) {
				Fade::To(0.0f, settings.fadeTime * 1.5f);
			}
			SetPhase(Phase::kHold);
		}

		// Immersive Interactions plays a pickup animation in 3rd person (from a perk entry's script) and with its "well
		// timed" option on picks the item up halfway through, else right away: the wait for the rest of it
		bool ImmersiveInteractions()
		{
			static const bool loaded = [] {
				const auto handler = RE::TESDataHandler::GetSingleton();
				const bool found = handler && (handler->LookupLoadedModByName("ImmersiveInteractions.esp") ||
				                                  handler->LookupLoadedLightModByName("ImmersiveInteractions.esp"));
				if (found) {
					logs::info("Immersive Interactions is loaded: 3rd person pickups wait for its animation");
				}
				return found;
			}();
			return loaded;
		}

		float PickupAnimationWait()
		{
			const auto camera = RE::PlayerCamera::GetSingleton();
			if (!camera || camera->IsInFirstPerson() || !ImmersiveInteractions()) {
				return 0.0f;
			}
			const float wait = Settings::Get().animationWait;
			if (wait <= 0.0f) {
				return 0.0f;
			}
			const auto wellTimed = RE::TESForm::LookupByEditorID<RE::TESGlobal>("AR_welltimed");
			return wait + (wellTimed && wellTimed->value == 0.0f ? kLateAnimationWait : 0.0f);
		}

		// Dynamic Looting and Harvesting Animations (LootingAnimations.esp) plays a harvest animation in 3rd person (from
		// a perk entry's script) and harvests the plant about a second in, while its item is shown in the right hand. Its
		// global LA_AnimTrigger (which picks the animation in its OAR conditions) stays set until the animation is over.
		RE::TESGlobal* LootingAnimationTrigger()
		{
			static RE::TESGlobal* const trigger = [] {
				const auto handler = RE::TESDataHandler::GetSingleton();
				const auto found = handler ? handler->LookupForm<RE::TESGlobal>(0x801, "LootingAnimations.esp") : nullptr;
				if (found) {
					logs::info("Dynamic Looting and Harvesting Animations is loaded: harvests wait for its animation");
				}
				return found;
			}();
			return trigger;
		}

		bool LootingAnimationPlays()
		{
			const auto trigger = LootingAnimationTrigger();
			return trigger && trigger->value != 0.0f;
		}

		// a_forSale: Purchaseable Store-Display-Items would have asked to buy it; that happens when it goes into the backpack
		bool Begin(RE::PlayerCharacter* a_player, RE::TESObjectREFR* a_ref, std::int32_t a_count, bool a_arg3, bool a_playSound, bool a_forSale)
		{
			if (!CanInspect(a_player, a_ref, a_forSale)) {
				return false;
			}
			const auto model = a_ref->Get3D();
			SetWeaponKind(a_ref->GetBaseObject());
			if (!MakeItem(model, true, !state.weapon)) {
				logs::warn("Couldn't copy the model of {:08X}, normal pickup", a_ref->GetFormID());
				state = {};
				return false;
			}
			state.source = Source::kWorld;
			state.ref = a_ref->GetHandle();
			state.baseID = a_ref->GetBaseObject()->GetFormID();
			state.count = a_count;
			state.arg3 = a_arg3;
			state.playSound = a_playSound;
			state.name = a_ref->GetDisplayFullName() ? a_ref->GetDisplayFullName() : "";
			state.worldModel.reset(model);
			state.forSale = a_forSale;
			logs::info("Inspecting {} ({:08X}, x{}{}), size {:.1f}, scale {:.2f}", state.name, a_ref->GetFormID(), a_count, a_forSale ? ", for sale" : "",
				state.radius, state.scale);
			// an item for sale skipped the activation perks, Immersive Interactions' pickup animation among them
			state.animationWait = a_forSale ? 0.0f : PickupAnimationWait();
			if (state.animationWait > 0.0f) {
				// the item lies there until the other mod's pickup animation is done; the player stands still meanwhile
				DisableControls();
				SetPhase(Phase::kWaitAnimation);
				return true;
			}
			model->SetAppCulled(true);
			TakeControl();
			return true;
		}

		// The item selected in the inventory list. SkyUI sorts its list, so its own "selectedEntry" (with the form ID) is
		// asked first; the vanilla list's selected index matches the menu's item array.
		RE::TESBoundObject* SelectedInventoryItem()
		{
			const auto ui = RE::UI::GetSingleton();
			const auto menu = ui ? ui->GetMenu<RE::InventoryMenu>() : nullptr;
			const auto list = menu ? menu->GetRuntimeData().itemList : nullptr;
			if (!list) {
				return nullptr;
			}
			RE::GFxValue entry;
			RE::GFxValue formID;
			if (list->root.GetMember("selectedEntry", &entry) && entry.IsObject() && entry.GetMember("formId", &formID) && formID.IsNumber()) {
				const auto id = static_cast<RE::FormID>(static_cast<std::int64_t>(formID.GetNumber()));
				if (const auto form = RE::TESForm::LookupByID<RE::TESBoundObject>(id)) {
					return form;
				}
			}
			if (const auto item = list->GetSelectedItem(); item && item->data.objDesc) {
				return item->data.objDesc->object;
			}
			return nullptr;
		}

		// the item's own world model, loaded from disk, when the preview hasn't loaded the selected item
		RE::NiPointer<RE::NiAVObject> LoadModel(RE::TESBoundObject* a_object)
		{
			const char* path = nullptr;
			if (const auto armor = a_object->As<RE::TESObjectARMO>()) {
				const auto base = Player()->GetActorBase();
				const bool female = base && base->GetSex() == RE::SEX::kFemale;
				path = armor->worldModels[female ? RE::SEXES::kFemale : RE::SEXES::kMale].GetModel();
				if (!path || !*path) {
					path = armor->worldModels[RE::SEXES::kMale].GetModel();
				}
			} else if (const auto model = a_object->As<RE::TESModel>()) {
				path = model->GetModel();
			}
			if (!path || !*path) {
				return nullptr;
			}
			RE::NiPointer<RE::NiNode> loaded;
			if (RE::BSModelDB::Demand(path, loaded, RE::BSModelDB::DBTraits::ArgsType{}) != RE::BSResource::ErrorCode::kNone || !loaded) {
				logs::warn("Couldn't load model {}", path);
				return nullptr;
			}
			// the database keeps the original: measure a copy placed at the origin
			const auto copy = loaded->Clone();
			RE::NiPointer<RE::NiAVObject> node{ copy ? netimmerse_cast<RE::NiAVObject*>(copy) : nullptr };
			if (node) {
				node->local = {};
				RE::NiUpdateData update{};
				node->Update(update);
			}
			return node;
		}

		// Scrolling up on an item in the inventory: the menu closes and the item comes to the hand
		bool BeginFromInventory(RE::Inventory3DManager* a_manager)
		{
			const auto& settings = Settings::Get();
			const auto  player = Player();
			if (!settings.enabled || !settings.inventoryInspect || Active() || !player || !PlayerFree(player) || !InventoryOpen()) {
				return false;
			}
			const auto selected = SelectedInventoryItem();
			if (selected && ((settings.skipWeapons && selected->IsWeapon()) || (settings.skipArmor && selected->IsArmor()))) {
				return false;  // the vanilla zoom for kinds the settings leave alone
			}
			// the preview's model of the selected item; the list can still hold models of items shown before
			RE::NiPointer<RE::NiAVObject> model;
			bool                          preview = false;
			auto&                         models = a_manager->GetRuntimeData().loadedModels;
			for (auto& entry : models) {
				if (entry.spModel && selected && (entry.itemBase == selected || entry.modelObj == selected)) {
					model = entry.spModel;
					preview = true;
				}
			}
			if (!model && selected) {
				model = LoadModel(selected);
			}
			if (!model && !selected && !models.empty()) {
				model = models.back().spModel;  // no selection to compare with: the newest preview model
				preview = true;
			}
			if (!model || !MakeItem(model.get(), false)) {
				logs::warn("No model for the selected inventory item {:08X}, normal zoom", selected ? selected->GetFormID() : 0);
				state = {};
				return false;
			}
			state.source = Source::kInventory;
			state.baseID = selected ? selected->GetFormID() : 0;
			state.name = ItemName(selected);
			// the inventory camera looks along +Y with +Z up: keep the turn the preview had (a model we loaded starts upright)
			state.rotation = preview ? model->world.rotate : NiMatrix3{};
			SetPhase(Phase::kWaitMenu);
			CloseInventoryMenus();
			logs::info("Inspecting {} ({:08X}) from the inventory ({} model), size {:.1f}, scale {:.2f}", state.name, state.baseID,
				preview ? "preview" : "loaded", state.radius, state.scale);
			return true;
		}

		// ---- QuickLoot: from a container ----

		std::atomic<bool> containerPending{ false };  // QuickLoot handed us an item, the inspection starts with the next task
		// the container searched last (native handle): taking more from it only reaches in quickly. Forgotten when
		// QuickLoot shows another container and on game load.
		std::atomic<std::uint32_t> searchedContainer{ 0 };

		// the container's model file, lower case ("" without one)
		std::string ContainerModel(const RE::TESObjectREFR* a_container)
		{
			const auto base = a_container->GetBaseObject();
			const auto model = base ? base->As<RE::TESModel>() : nullptr;
			std::string path = model && model->GetModel() ? model->GetModel() : "";
			std::ranges::transform(path, path.begin(), [](char a_c) { return static_cast<char>(std::tolower(static_cast<unsigned char>(a_c))); });
			return path;
		}

		// a dead NPC or creature, or a container that looks like one (burnt corpses, draugr ambush bodies...)
		bool IsBody(const RE::TESObjectREFR* a_container)
		{
			if (a_container->As<RE::Actor>()) {
				return true;
			}
			const auto model = ContainerModel(a_container);
			return model.contains("corpse") || model.contains("bodies\\") || model.contains("dead");
		}

		// A real chest, not a barrel, sack, urn, strongbox, chest of drawers or wardrobe: by its model (vanilla chests
		// are chest01, upperchest01, noblechest01, dwechest01, ruins_largechest / smallchest, the Dwemer and Falmer
		// containers), at least kBigChestWidth wide (small modded jewelry chests stay normal)
		constexpr float kBigChestWidth = 80.0f;

		bool IsBigChest(const RE::TESObjectREFR* a_container)
		{
			if (a_container->As<RE::Actor>()) {
				return false;
			}
			const auto model = ContainerModel(a_container);
			const bool chest = (model.contains("chest") && !model.contains("drawer")) || model.contains("trunk") ||
			                   model.contains("dwecontainer01") || model.contains("falmercontainer");
			if (!chest) {
				return false;
			}
			const auto base = a_container->GetBaseObject();
			const auto& bound = base->boundData;
			const float width = static_cast<float>(std::max(bound.boundMax.x - bound.boundMin.x, bound.boundMax.y - bound.boundMin.y));
			return width == 0.0f || width * a_container->GetScale() >= kBigChestWidth;  // no bounds set: the model's name decides
		}

		// CanInspect for an item still in a container (QuickLoot's menu is open, which MenuOpen counts)
		bool CanInspectContainerItem(const ContainerItem& a_item)
		{
			const auto& settings = Settings::Get();
			const auto  player = Player();
			const auto  ui = RE::UI::GetSingleton();
			if (!settings.enabled || !settings.quickLoot || Active() || containerPending || !InspectKeyAllows() || !player || !ui || ui->GameIsPaused() ||
				!a_item.object || !KindInspected(a_item.object)) {
				return false;
			}
			if (!settings.alwaysInspect && Seen::Contains(a_item.object->GetFormID())) {
				return false;
			}
			if (!settings.inspectStolen && a_item.stealing) {
				return false;
			}
			const auto container = a_item.container.get();
			if (!container || (settings.bigChestsOnly ? !IsBigChest(container.get()) : !settings.lootBodies && IsBody(container.get()))) {
				return false;  // the settings leave this kind of container to QuickLoot
			}
			return PlayerFree(player) && !(settings.skipInCombat && player->IsInCombat());
		}

		// where the item comes out of the container: a body's chest, else the middle of the container's model
		NiPoint3 ContainerSpot(RE::TESObjectREFR* a_container)
		{
			if (const auto actor = a_container->As<RE::Actor>(); actor && actor->Get3D(false)) {
				const auto root = actor->Get3D(false);
				const auto spine = root->GetObjectByName("NPC Spine2 [Spn2]");
				return spine ? spine->world.translate : root->worldBound.center;
			}
			if (const auto model = a_container->Get3D()) {
				return model->worldBound.center;
			}
			return a_container->GetPosition() + NiPoint3{ 0.0f, 0.0f, 30.0f };
		}

		// The game's own searching animation first: crouched, rummaging in front of the knees (searchingchest.hkx; it
		// loops, it's stopped after the set time). Only in 3rd person: the 1st person view doesn't show it.
		bool StartSearching(RE::PlayerCharacter* a_player, RE::ObjectRefHandle a_container)
		{
			const bool  again = a_container.native_handle() == searchedContainer.load();
			const auto& settings = Settings::Get();
			const float time = again ? settings.searchAgainTime : settings.searchTime;
			searchedContainer = a_container.native_handle();
			if (time <= 0.0f || FirstPerson() || a_player->IsSneaking() || a_player->AsActorState()->IsWeaponDrawn()) {
				return false;
			}
			const char* idle = "IdleSearchingChest";
			if (!a_player->NotifyAnimationGraph(idle)) {
				logs::info("The searching animation {} didn't start", idle);
				return false;
			}
			state.searching = true;
			state.searchTime = time;
			state.animationWait = time + kSearchStandUp;
			const NiPoint3 to = state.containerSpot - a_player->GetPosition();
			state.searchHeading = std::atan2(to.x, to.y);
			logs::info("Searching the container{} ({:.2f} s)", again ? " again" : "", time);
			return true;
		}

		// while searching the player turns to face the container
		void TurnToContainer(RE::PlayerCharacter* a_player, float a_delta)
		{
			const float heading = a_player->GetAngleZ();
			const float turn = std::remainder(state.searchHeading - heading, 2.0f * 3.14159265f) * std::min(1.0f, a_delta * kSearchTurn);
			if (std::abs(turn) > 1e-4f) {
				a_player->SetHeading(heading + turn);
			}
		}

		bool BeginFromContainer(const ContainerItem& a_item)
		{
			const auto player = Player();
			const auto container = a_item.container.get();
			if (!player || !container || Active() || !PlayerFree(player)) {
				return false;
			}
			const auto object = a_item.object;
			SetWeaponKind(object);
			const auto model = LoadModel(object);
			if (!model || !MakeItem(model.get(), true, !state.weapon)) {
				logs::warn("No model for {:08X} from the container, taken as usual", object->GetFormID());
				state = {};
				return false;
			}
			state.source = Source::kContainer;
			state.ref = a_item.container;
			state.take = a_item;
			state.baseID = object->GetFormID();
			state.count = a_item.count;
			state.name = ItemName(object);
			state.containerSpot = ContainerSpot(container.get());
			ContainerLid::KeepOpen(container.get());  // QuickLoot opened it and would close it with its menu now
			QuickLoot::Pause(true);  // its menu would stay on screen and take the keys
			logs::info("Inspecting {} ({:08X}, x{}) from container {:08X}{}, size {:.1f}, scale {:.2f}", state.name, state.baseID, state.count,
				container->GetFormID(), a_item.stealing ? " (stealing)" : "", state.radius, state.scale);
			if (StartSearching(player, a_item.container)) {
				DisableControls();  // the player stays where they are meanwhile
				SetPhase(Phase::kWaitAnimation);
				return true;
			}
			TakeControl();
			return true;
		}

		// ---- harvesting a plant ----

		std::atomic<bool> harvestPending{ false };  // a harvest was held back, the inspection starts with the next task

		// what harvesting gives, to show: a leveled list's first item (the real harvest rolls its own)
		RE::TESBoundObject* ShownProduce(RE::TESBoundObject* a_produce, int a_depth = 0)
		{
			const auto list = a_produce ? a_produce->As<RE::TESLevItem>() : nullptr;
			if (!list) {
				return a_produce;
			}
			if (a_depth < 4) {
				for (const auto& entry : list->entries) {
					const auto form = entry.form ? entry.form->As<RE::TESBoundObject>() : nullptr;
					if (const auto found = ShownProduce(form, a_depth + 1)) {
						return found;
					}
				}
			}
			return nullptr;
		}

		// CanInspect for a plant's ingredient, before the harvest
		bool CanInspectHarvest(RE::PlayerCharacter* a_player, RE::TESObjectREFR* a_plant, RE::TESBoundObject* a_produce)
		{
			const auto& settings = Settings::Get();
			if (!settings.enabled || settings.skipHarvest || Active() || harvestPending || !InspectKeyAllows() || !a_produce || !KindInspected(a_produce) ||
				a_plant->IsDisabled() || Harvested(a_plant) || !a_plant->Get3D()) {
				return false;
			}
			if (!settings.alwaysInspect && Seen::Contains(a_produce->GetFormID())) {
				return false;
			}
			if (!settings.inspectStolen && a_plant->IsCrimeToActivate()) {
				return false;  // somebody's crops: normal harvest
			}
			return PlayerFree(a_player) && !(settings.skipInCombat && a_player->IsInCombat()) && !MenuOpen();
		}

		// the middle of the plant's model: where its ingredient comes from
		NiPoint3 PlantSpot(RE::TESObjectREFR* a_plant)
		{
			const auto model = a_plant->Get3D();
			return model ? model->worldBound.center : a_plant->GetPosition() + NiPoint3{ 0.0f, 0.0f, 20.0f };
		}

		bool BeginHarvest(RE::ObjectRefHandle a_plant, const Harvest& a_harvest, RE::TESBoundObject* a_produce)
		{
			const auto player = Player();
			const auto plant = a_plant.get();
			if (!player || !plant || Active() || !CanInspectHarvest(player, plant.get(), a_produce)) {
				return false;
			}
			const auto model = LoadModel(a_produce);
			if (!model || !MakeItem(model.get(), true)) {
				logs::warn("No model for {:08X} from plant {:08X}, harvested as usual", a_produce->GetFormID(), plant->GetFormID());
				state = {};
				return false;
			}
			state.source = Source::kHarvest;
			state.ref = a_plant;
			state.harvest = a_harvest;
			state.baseID = a_produce->GetFormID();
			state.count = a_harvest.count;
			state.name = ItemName(a_produce);
			state.containerSpot = PlantSpot(plant.get());
			logs::info("Inspecting {} ({:08X}) from plant {:08X}, size {:.1f}, scale {:.2f}", state.name, state.baseID, plant->GetFormID(), state.radius,
				state.scale);
			state.lootingAnimation = LootingAnimationPlays();
			state.animationWait = state.lootingAnimation ? kLootingStandUp : PickupAnimationWait();
			if (state.animationWait > 0.0f) {
				// the other mod's harvest animation plays out first (it shows the item in the hand meanwhile)
				DisableControls();
				SetPhase(Phase::kWaitAnimation);
				return true;
			}
			TakeControl();
			return true;
		}

		// A plant was activated: unless the item goes straight into the inventory, the harvest waits for the inspection.
		// Can come from a script (Dynamic Looting harvests from its perk's script), so the inspection starts in a task.
		bool OfferHarvest(RE::TESBoundObject* a_plant, RE::TESBoundObject* a_produce, RE::TESObjectREFR* a_ref, RE::TESObjectREFR* a_activator,
			const Harvest& a_harvest)
		{
			const auto player = Player();
			const auto shown = ShownProduce(a_produce);
			if (!a_ref || !player || a_activator != player || !CanInspectHarvest(player, a_ref, shown)) {
				return false;
			}
			harvestPending = true;
			SKSE::GetTaskInterface()->AddTask([handle = a_ref->GetHandle(), a_harvest, shown]() {
				harvestPending = false;
				if (!BeginHarvest(handle, a_harvest, shown)) {
					HarvestNow(handle, a_harvest);  // harvested the normal way after all
				}
			});
			logs::debug("Held back the harvest of {:08X} ({:08X})", a_ref->GetFormID(), a_plant->GetFormID());
			return true;
		}

		// TESFlora / TESObjectTREE::Activate: harvesting (both carry the ingredient as TESProduceForm)
		template <class T>
		struct HarvestActivate
		{
			static bool thunk(RE::TESBoundObject* a_this, RE::TESObjectREFR* a_ref, RE::TESObjectREFR* a_activator, std::uint8_t a_arg3,
				RE::TESBoundObject* a_object, std::int32_t a_count)
			{
				const auto produce = static_cast<RE::TESProduceForm*>(static_cast<T*>(a_this))->produceItem;
				const Harvest harvest{ .plant = a_this, .activate = Original, .arg3 = a_arg3, .object = a_object, .count = a_count };
				if (produce && OfferHarvest(a_this, produce, a_ref, a_activator, harvest)) {
					return true;  // harvested later, when the item goes into the backpack
				}
				return func(a_this, a_ref, a_activator, a_arg3, a_object, a_count);
			}
			static bool Original(RE::TESBoundObject* a_this, RE::TESObjectREFR* a_ref, RE::TESObjectREFR* a_activator, std::uint8_t a_arg3,
				RE::TESBoundObject* a_object, std::int32_t a_count)
			{
				return func(a_this, a_ref, a_activator, a_arg3, a_object, a_count);
			}
			static inline REL::Relocation<decltype(thunk)> func;
		};

		// ---- Purchaseable Store-Display-Items: items for sale ----

		// the item for sale whose activation skipped the store's buy: its pickup (right after) goes to the hand
		RE::ObjectRefHandle forSaleRef;

		// the last answer of TakeForSale: each of PSDI's activation entries asks, one activation gets one answer
		RE::ObjectRefHandle forSaleAsked;
		std::uint32_t       forSaleAskedAt{ 0 };
		bool                forSaleAnswer{ false };
		constexpr std::uint32_t kForSaleAnswerTime = 250;  // ms

		// The player activates an item: if the store would ask to buy it, PSDI's activation entries are skipped (the
		// store's buy box with them), so the game's own activation picks it up into the hand
		bool TakeForSale(RE::TESObjectREFR* a_target)
		{
			const auto handle = a_target->GetHandle();
			const auto now = RE::GetDurationOfApplicationRunTime();
			if (handle == forSaleAsked && now - forSaleAskedAt < kForSaleAnswerTime) {
				return forSaleAnswer;
			}
			forSaleAsked = handle;
			forSaleAskedAt = now;
			forSaleAnswer = false;
			forSaleRef = {};
			const auto& settings = Settings::Get();
			const auto  player = Player();
			if (!settings.enabled || !settings.buyStoreItems || Active() || !player || !StoreDisplay::ForSale(a_target) ||
				!CanInspect(player, a_target, true)) {
				return false;
			}
			forSaleRef = handle;
			forSaleAnswer = true;
			logs::info("{:08X} is for sale: it goes to the hand, the store asks to buy it when it goes into the backpack", a_target->GetFormID());
			return true;
		}

		// what the player has of an item
		std::int32_t ItemCount(RE::TESBoundObject* a_object)
		{
			const auto counts = Player()->GetInventoryCounts([&](RE::TESBoundObject& a_item) { return &a_item == a_object; });
			const auto found = counts.find(a_object);
			return found != counts.end() ? found->second : 0;
		}

		// Activating an item (PlayerCharacter::ActivatePickRef) asks the perks' activation entries first (AE 41003, the
		// only user of the Activate entry point): a perk that takes it (PSDI's buy box) replaces the game's own activation.
		// The perk visitor (AE 23527) asks each entry's conditions through its vtable, so this hook needs no address
		// inside a function and works the same on SE and AE. Arguments: the player, the activated item.
		struct EntryConditions
		{
			static bool thunk(RE::BGSEntryPointPerkEntry* a_this, std::uint32_t a_numArgs, void* a_args)
			{
				if (a_numArgs == 2 && a_args && StoreDisplay::IsActivationEntry(a_this)) {
					const auto target = static_cast<RE::TESForm**>(a_args)[1];
					const auto ref = target ? target->AsReference() : nullptr;
					if (ref && TakeForSale(ref)) {
						return false;
					}
				}
				return func(a_this, a_numArgs, a_args);
			}
			static inline REL::Relocation<decltype(thunk)> func;
		};

		void PickUpObject::thunk(RE::PlayerCharacter* a_this, RE::TESObjectREFR* a_object, std::int32_t a_count, bool a_arg3, bool a_playSound)
		{
			const bool forSale = forSaleRef && a_object && forSaleRef == a_object->GetHandle();
			forSaleRef = {};
			if (a_this == Player() && Begin(a_this, a_object, a_count, a_arg3, a_playSound, forSale)) {
				return;  // picked up later, when it goes into the backpack
			}
			if (forSale) {
				// never taken for free: the store's own buy, as if we hadn't been here
				StoreDisplay::Buy(a_object, [] {});
				return;
			}
			func(a_this, a_object, a_count, a_arg3, a_playSound);
		}

		// The inventory menu's "Item Zoom" (scroll up on the item). Zooming in takes the item in the hand instead.
		struct ItemZoom
		{
			static bool thunk(RE::Inventory3DManager* a_this, RE::VR_DEVICE a_device)
			{
				// One scroll can reach this from more than one place (menu message and zoom callback): once we took the
				// item, every further zoom is swallowed, or the vanilla zoom would start in the closing menu.
				if (Active()) {
					return false;
				}
				const float progress = a_this ? a_this->GetRuntimeData().zoomProgress : -1.0f;
				if (progress == 0.0f && BeginFromInventory(a_this)) {
					return false;
				}
				return func(a_this, a_device);
			}
			static inline REL::Relocation<decltype(thunk)> func;
		};

		// ---- per frame ----

		void ApplyRotationInput(float a_delta)
		{
			const auto& settings = Settings::Get();
			if (state.looking) {
				ApplyLookInput(a_delta);
				state.distanceOffset = std::clamp(state.distanceOffset - state.wheel * kWheelStep, -kMaxCloser, kMaxFarther);
				return;  // the mouse turns the head now
			}
			const float invert = settings.invertY ? -1.0f : 1.0f;
			const float yaw = state.mouseX * kMouseSpeed * settings.mouseSensitivity + state.stickX * kStickSpeed * a_delta * settings.gamepadSensitivity;
			const float pitch = -(state.mouseY * kMouseSpeed * settings.mouseSensitivity + state.stickY * kStickSpeed * a_delta * settings.gamepadSensitivity) * invert;
			if (state.weapon) {
				// the wrist turns: left / right twists the blade around itself, up / down bends the wrist
				const float twistMin = state.stringBow ? -kBowTwistToSide : -kMaxTwist;
				const float twistMax = state.stringBow ? kBowTwistToEye : kMaxTwist;
				state.twistTarget = std::clamp(state.twistTarget + yaw, twistMin, twistMax);
				state.bendTarget = std::clamp(state.bendTarget + pitch, -kMaxBend, kMaxBend);
				state.distanceOffset = std::clamp(state.distanceOffset - state.wheel * kWheelStep, -kMaxCloser, kMaxFarther);
				return;
			}
			// camera frame: x right, y forward, z up. Dragging right turns the near side to the right, dragging up turns it up.
			state.rotation = AxisAngle({ 0.0f, 0.0f, 1.0f }, yaw) * AxisAngle({ 1.0f, 0.0f, 0.0f }, pitch) * state.rotation;
			state.distanceOffset = std::clamp(state.distanceOffset - state.wheel * kWheelStep, -kMaxCloser, kMaxFarther);

			// the hand "works" the item: the fingers roll it while it turns (see UpdateFingers), the wrist leans with it.
			// Mouse input comes in uneven bursts, so everything follows smoothly instead of jumping with it.
			if (a_delta > 0.0f) {
				const float speed = (std::abs(yaw) + std::abs(pitch)) / a_delta;  // radians per second
				const float wanted = std::clamp(speed / 2.5f, 0.0f, 1.0f);
				const float rate = wanted > state.activity ? kActivityRise : kActivityFall;
				state.activity += (wanted - state.activity) * std::min(1.0f, a_delta * rate);
				const float lean = std::min(1.0f, a_delta * kLeanFollow);
				state.leanX += (std::clamp(yaw / a_delta / 6.0f, -1.0f, 1.0f) - state.leanX) * lean;
				state.leanY += (std::clamp(pitch / a_delta / 6.0f, -1.0f, 1.0f) - state.leanY) * lean;
				if (std::abs(yaw) + std::abs(pitch) > 1e-4f) {
					state.turnDirection = (std::abs(yaw) >= std::abs(pitch) ? yaw : -pitch) >= 0.0f ? 1.0f : -1.0f;
				}
				// the fingers' wave moves with the item: turning drives it, the turn direction picks which way it runs
				// (capped: a fast flick must not make them twitch)
				state.fingerPhase += std::min(speed, kMaxFingerWave) * a_delta * state.turnDirection;
			}
		}

		// fades the hand activity out while the item isn't being turned
		void CalmDown(float a_delta)
		{
			state.activity -= state.activity * std::min(1.0f, a_delta * kActivityFall);
			const float lean = std::min(1.0f, a_delta * kLeanFollow);
			state.leanX -= state.leanX * lean;
			state.leanY -= state.leanY * lean;
		}

		// Telekinesis: an open hand whose fingers slowly flex one after another, a bit more and a bit faster while the item
		// turns. Without telekinesis a loose grip. Every finger eases towards its target, so nothing jumps.
		void UpdateFingers(float a_delta)
		{
			std::array<float, 5> target{};
			// 3rd person, put in the backpack: the hand closes into a fist around the item as soon as it comes down onto
			// the palm, all the way to the pocket (and opens with the arm's animation)
			const bool fist = state.thirdPerson && !state.weapon &&
			                  (state.phase == Phase::kStow || state.phase == Phase::kReturn || (state.phase == Phase::kSettle && state.afterSettle == Phase::kStow));
			if (fist) {
				target = kPocketFist;
			} else if (state.settled || state.phase == Phase::kSettle) {
				target.fill(kCupCurl);  // closing around the item that comes down onto the palm
			} else if (state.telekinesis) {
				// A wave running over the fingers (thumb to little finger or back, with the turn direction); per joint, so
				// the fingertip moves three times this. At rest it barely moves; while the item turns the fingers clearly
				// "roll" it (ApplyRotationInput pushes the wave along with the turn).
				state.fingerPhase += a_delta * 1.2f * state.turnDirection;
				const float amount = 0.08f + 0.32f * state.activity;
				for (int i = 0; i < 5; ++i) {
					const float wave = std::sin(state.fingerPhase - static_cast<float>(i) * 0.9f);
					target[i] = kOpenCurl + amount * (0.5f + 0.5f * wave) * (i == 0 ? 0.5f : 1.0f);
				}
			} else if (state.weapon) {
				target = {};  // the fingers take the game's weapon grip (ArmPose::Goal::weaponGrip)
			} else {
				target.fill(kCurl);
			}
			const float follow = std::min(1.0f, a_delta * (fist ? kFistFollow : kFingerFollow));
			for (int i = 0; i < 5; ++i) {
				state.fingers[i] += (target[i] - state.fingers[i]) * follow;
			}
		}

		RE::NiAVObject* HandMagicNode()
		{
			const auto root = ArmRoot();
			return root ? root->GetObjectByName("NPC R MagicNode [RMag]") : nullptr;
		}

		// The item reached the hand: its pickup sound, played the way the game plays it when you pick something up (its
		// own sound or the default for its kind). Our own handle for the same sound stayed silent the first time.
		void PlayItemSound()
		{
			const auto player = Player();
			const auto item = RE::TESForm::LookupByID<RE::TESBoundObject>(state.baseID);
			if (player && item && Settings::Get().itemSound) {
				player->PlayPickUpSound(item, true, false);
			}
		}

		// the telekinesis grab sound, for items that float (picked up, put in the backpack, put back down)
		void PlayTelekinesisSound(RE::NiAVObject* a_node)
		{
			if (state.telekinesis && Settings::Get().telekinesisSound) {
				Telekinesis::PlayGrabSound(a_node);
			}
		}

		float ItemSize();

		// 3rd person: phases in which the head looks at the item in the hand. Put in the backpack, the head lets go
		// right away and turns back while the hand goes to the pocket.
		bool LooksAtItem()
		{
			switch (state.phase) {
			case Phase::kRaise:
			case Phase::kHold:
			case Phase::kBuying:
			case Phase::kPutBack:
				return true;
			case Phase::kSettle:
				return state.afterSettle != Phase::kStow;
			case Phase::kSwitchView:
				return !state.switched;
			default:
				return false;
			}
		}

		void ClearInput()
		{
			state.mouseX = state.mouseY = state.wheel = 0.0f;
		}

		// the hand stops holding: the item goes into the backpack (kStow) or back where it was (kPutBack)
		void LeaveHold(Phase a_next)
		{
			if (a_next == Phase::kStow) {
				WakeWorldForTheft();
			}
			StopLooking(false);
			state.inHandSet = false;
			if (a_next == Phase::kPutBack && state.source == Source::kHarvest) {
				// back into the plant (also when it came out of the hand), shrinking like it came
				state.start = ItemAt(state.containerSpot, state.last.rotate, state.scale * kRiseFromScale);
			}
			if (state.weapon || a_next == Phase::kPutBack) {
				// a weapon is already in the hand; put back, the item floats straight back down (telekinesis keeps it
				// until it lies there)
				SetPhase(a_next);
			} else {
				state.afterSettle = a_next;
				SetPhase(Phase::kSettle);
			}
		}

		// An item for sale goes into the backpack: the store's buy box first (its price, its gold to the vendor). The
		// hand keeps holding the item until it's answered.
		void BeginBuying()
		{
			const auto ref = state.ref.get();
			const auto object = ref ? ref->GetBaseObject() : nullptr;
			static std::uint32_t requests = 0;
			const auto           request = ++requests;
			state.countBefore = object ? ItemCount(object) : 0;
			state.buyRequest = request;
			state.buyAnswered = false;
			StopLooking(false);
			WorldPause::End();  // the vendor takes the gold and may say something
			const auto answered = [request]() {
				if (state.phase == Phase::kBuying && state.buyRequest == request) {
					state.buyAnswered = true;
				}
			};
			if (!ref || !StoreDisplay::Buy(ref.get(), answered)) {
				logs::warn("The store can't sell it: put back");
				LeaveHold(Phase::kPutBack);
				return;
			}
			SetPhase(Phase::kBuying);
		}

		// the store's script sold it: paid, the item is the player's (and already in the inventory, or on its way)
		bool Bought()
		{
			const auto player = Player();
			const auto object = RE::TESForm::LookupByID<RE::TESBoundObject>(state.baseID);
			if (object && ItemCount(object) > state.countBefore) {
				return true;
			}
			const auto ref = state.ref.get();
			return !ref || ref->IsDisabled() || ref->IsMarkedForDeletion() || (player && ref->GetOwner() == player->GetActorBase());
		}

		void OnPlayerUpdate(float a_delta)
		{
			Telekinesis::Update(a_delta);
			ContainerLid::Update(a_delta);
			Fade::Update(a_delta);
			if (!Active()) {
				return;
			}
			const auto& settings = Settings::Get();
			const auto  player = Player();
			const bool  world = state.source == Source::kWorld;
			const bool  container = state.source == Source::kContainer;
			const bool  harvest = state.source == Source::kHarvest;
			const auto  ref = state.ref.get();
			state.time += a_delta;
			state.clock += a_delta;
			if (state.awake >= 0.0f) {
				state.awake += a_delta;
			}
			if (state.hudModePushed) {
				HUD::KeepHoldMode();
			}

			// (an item for sale is picked up by the store's script while it's being bought)
			const bool beingBought = state.phase == Phase::kBuying || state.bought;
			if (!player || player->IsDead() || (state.source != Source::kInventory && !state.pickedUp && !beingBought && !ref)) {
				logs::info("Inspection ended: {}", player && !player->IsDead() ? "the item is gone" : "the player died");
				Finish();
				return;
			}
			if (state.phase != Phase::kWaitCamera && state.phase != Phase::kWaitMenu && state.phase != Phase::kWaitAnimation &&
				state.phase != Phase::kFadeOut && state.phase != Phase::kSwitchView && !CameraStillOurs()) {
				// something took the camera away: give up, the item stays where it was
				logs::info("Inspection ended: the camera left {} person", state.thirdPerson ? "3rd" : "1st");
				Finish();
				return;
			}

			switch (state.phase) {
			case Phase::kFadeOut:
				if (Fade::Black() || state.time > kMaxFadeWait) {
					RE::PlayerCamera::GetSingleton()->ForceFirstPerson();
					SetPhase(Phase::kWaitCamera);
				}
				break;
			case Phase::kFadeBack:
				if (Fade::Black() || state.time > kMaxFadeWait) {
					Finish();
				}
				break;
			case Phase::kWaitAnimation:
				if (state.searching) {
					TurnToContainer(player, a_delta);
					if (state.time >= state.searchTime) {
						player->NotifyAnimationGraph("IdleForceDefaultState");  // up again before the camera moves in
						state.searching = false;
					}
				}
				if (state.lootingAnimation && state.time < kMaxLootingWait && LootingAnimationPlays()) {
					state.animationWait = state.time + kLootingStandUp;  // until a moment after Dynamic Looting's animation
				}
				if (state.time >= state.animationWait) {
					// in 3rd person that mod showed the item in the right hand: it comes up from there
					state.riseFromHand = state.lootingAnimation && !FirstPerson();
					if (state.worldModel) {
						state.worldModel->SetAppCulled(true);
					}
					TakeControl();
				}
				break;
			case Phase::kWaitMenu:
				if (!InventoryMenusOpen()) {
					TakeControl();
				} else if (state.time > kCameraTimeout) {
					logs::warn("The inventory / Tab menu didn't close");
					Finish();
				}
				break;
			case Phase::kWaitCamera:
				if (state.thirdPerson ? BodyRoot() != nullptr : FirstPerson() && FirstPersonRoot()) {
					if (world) {
						// the item keeps the turn it lay with (in the frame it's turned in)
						const auto frames = state.thirdPerson ? ThirdPersonFrames() : std::nullopt;
						const auto frame = frames ? frames->view : MakeCameraFrame(state.start.translate, player->data.angle.x, player->data.angle.z);
						state.rotation = frame.basis.Transpose() * state.start.rotate;
					} else if (container || harvest) {
						// it comes up out of the container / plant, already turned the way it's held (upright) and growing;
						// after Dynamic Looting's animation out of the 3rd person hand, at its size there
						const auto frames = state.thirdPerson ? ThirdPersonFrames() : std::nullopt;
						const auto frame = frames ? frames->view : MakeCameraFrame(state.containerSpot, player->data.angle.x, player->data.angle.z);
						const auto body = state.riseFromHand && state.thirdPerson ? BodyRoot() : nullptr;
						const auto hand = body ? body->GetObjectByName("NPC R Hand [RHnd]") : nullptr;
					state.start = ItemAt(hand ? hand->world.translate : state.containerSpot, frame.basis * state.rotation,
							state.scale * (hand ? ItemSize() : kRiseFromScale));
					}
					HideWeapon();
					if (!state.thirdPerson) {
						Fade::To(0.0f, settings.fadeTime * 1.5f);  // the hand is already coming up when the screen clears
					}
					state.telekinesis = settings.telekinesis && !state.weapon;  // weapons are simply held by the grip
					if (state.telekinesis) {
						Telekinesis::Start(HandMagicNode(), { .size = settings.telekinesisSize, .light = settings.telekinesisLight });
						// from where the item lifts off: the 1st person hand node isn't in place yet right after the
						// camera switch, and a sound following it jumped there (the pickup sound came out broken)
						const auto lifted = state.ref.get();
						PlayTelekinesisSound(lifted && lifted->Get3D() ? lifted->Get3D() : player->Get3D(false));
					}
					SetPhase(Phase::kRaise);
				} else if (state.time > kCameraTimeout) {
					logs::warn("Camera didn't switch to 1st person");
					const auto pick = state;
					Finish();
					if (pick.source == Source::kWorld) {
						PickUpNow(pick.ref, pick.count, pick.arg3, pick.playSound, nullptr);
					} else if (pick.source == Source::kContainer) {
						GiveContainerItem(pick.take);  // QuickLoot left it to us
					} else if (pick.source == Source::kHarvest) {
						HarvestNow(pick.ref, pick.harvest);
					}
				}
				break;
			case Phase::kRaise:
				if (state.time >= settings.raiseTime) {
					PlayItemSound();
					SetPhase(Phase::kHold);
				}
				break;
			case Phase::kHold:
				ApplyRotationInput(a_delta);
				if (state.switchRequested && !state.storeRequested && !state.putBackRequested) {
					state.switchRequested = false;
					BeginSwitchView();
					break;
				}
				state.switchRequested = false;
				if (state.storeRequested || state.putBackRequested) {
					// an inventory item has nowhere else to go than the backpack
					const Phase next = state.storeRequested || state.source == Source::kInventory ? Phase::kStow : Phase::kPutBack;
					if (next == Phase::kStow && state.forSale && !state.bought) {
						BeginBuying();
					} else {
						LeaveHold(next);
					}
				}
				break;
			case Phase::kBuying:
				// the buy box pauses the game: this runs again once it's answered
				if (state.buyAnswered || state.time > kBuyTimeout) {
					if (state.buyAnswered && Bought()) {
						state.bought = true;
						logs::info("Bought");
						LeaveHold(Phase::kStow);
					} else {
						logs::info("{}: put back", state.buyAnswered ? "Not bought" : "The store didn't answer");
						LeaveHold(Phase::kPutBack);
					}
				}
				break;
			case Phase::kSwitchView:
				if (!state.switched) {
					if (!settings.fadeTransition || Fade::Black() || state.time > kMaxFadeWait) {
						SwitchViewNow();
					}
				} else if (state.switchToThird ? ThirdPersonCamera() && BodyRoot() && state.time >= kViewSettle : FirstPerson() && FirstPersonRoot()) {
					EndSwitchView();
				} else if (state.time > kCameraTimeout) {
					logs::warn("The camera didn't switch to {} person", state.switchToThird ? "3rd" : "1st");
					EndSwitchView();
				}
				break;
			case Phase::kSettle:
				if (state.time >= kSettleTime) {
					state.settled = true;
					Telekinesis::Stop();  // the item is in the hand now
					SetPhase(state.afterSettle);
				}
				break;
			case Phase::kStow:
				if (!state.pickedUp && state.time >= settings.stowTime * kStowPickUpAt && TheftSeen()) {
					state.pickedUp = true;
					DetachItem();
					if (world && state.bought) {
						Seen::Add(state.baseID);  // the store's script already put it in the inventory
					} else if (world) {
						auto hidden = state.worldModel;
						state.worldModel.reset();
						PickUpNow(state.ref, state.count, state.arg3, state.playSound, hidden);
						Seen::Add(state.baseID);
					} else if (container) {
						GiveContainerItem(state.take);
						ContainerLid::Release(state.ref);
						Seen::Add(state.baseID);
					} else if (harvest) {
						HarvestNow(state.ref, state.harvest);
						Seen::Add(state.baseID);
					}
					// from the player's body, not the 1st person hand: the hand is on its way out of view and the camera
					// goes back to 3rd person right after, and a sound following the hand jumped with it (broken sound)
					PlayTelekinesisSound(player->Get3D(false));
					logs::info("Put in the backpack");
				}
				if (state.time >= settings.stowTime && state.pickedUp) {
					SetPhase(Phase::kReturn);
					state.cameraIn = false;  // 3rd person: the camera flies back while the empty hand comes back
				}
				break;
			case Phase::kReturn:
				if (state.time >= settings.returnTime) {
					End();
				}
				break;
			case Phase::kCameraBack:
				if (state.cameraBlend <= 0.0f) {
					Finish();
				}
				break;
			case Phase::kPutBack:
				if (state.time >= settings.returnTime) {
					const auto landed = state.ref.get();
					PlayTelekinesisSound(landed && landed->Get3D() ? landed->Get3D() : player->Get3D(false));  // where it lands
					logs::info("Put back");
					if (container) {
						ContainerLid::Release(state.ref);  // it's back inside
					}
					End();
				}
				break;
			default:
				break;
			}
			if (state.phase != Phase::kHold) {
				CalmDown(a_delta);
			}
			UpdateLookReturn(a_delta);
			UpdateFingers(a_delta);
			if (state.thirdPerson) {
				// the head turns to the item while it's in the hand (and follows it back down when put back)
				const bool  look = settings.headLook && state.lookTargetSet && LooksAtItem();
				const float follow = std::min(1.0f, a_delta * (look ? kHeadFollow : kHeadReturn));
				state.headWeight += ((look ? 1.0f : 0.0f) - state.headWeight) * follow;
				const float step = a_delta / std::max(settings.cameraTime, 0.05f);
				state.cameraBlend = std::clamp(state.cameraBlend + (state.cameraIn ? step : -step), 0.0f, 1.0f);
				// the body settles into standing, then its animation slows to a stop; it goes on when the camera flies back
				state.stillClock += a_delta;
				const float still = settings.standStill && state.cameraIn && state.stillClock >= kIdleSettle ? 1.0f : 0.0f;
				const float stillStep = a_delta * kStillFollow;
				state.stillness = std::clamp(state.stillness + std::clamp(still - state.stillness, -stillStep, stillStep), 0.0f, 1.0f);
			}
			{
				const float follow = std::min(1.0f, a_delta * kTurnFollow);
				state.twist += (state.twistTarget - state.twist) * follow;
				state.bend += (state.bendTarget - state.bend) * follow;
			}
			ClearInput();
			if (state.phase != Phase::kRaise) {
				// a press while the hand comes up counts once it's up
				state.storeRequested = false;
				state.putBackRequested = false;
			}
		}

		void SetBowGrip(const ArmPose::HandFrame& a_hand);

		// The WEAPON node holds the handle where the game's own fist has it; with the fingers of the one-handed idle
		// (ArmPose kWeaponGrip) it sat across the fingers. Moved and turned into the fist as tuned in game (hand space:
		// +X towards the index finger / the blade, +Y towards the palm, +Z along the fingers), around the handle's point
		// in the middle of the fist (the model origin, where the hand holds it).
		const NiPoint3  kFistOffset{ -4.0f, 0.48f, -2.08f };  // the hand 4 towards the blade, 0.48 into the palm, 2.08 towards the wrist
		const NiPoint3  kBodyFistOffset{ -1.0f, 0.48f, -2.08f };  // 3rd person (same tilt / turn), tuned in game
		constexpr float kFistTilt = 18.0f;                    // degrees around -Y: the index finger's end towards the fingers
		constexpr float kFistTurn = -4.0f;                    // degrees around +Z: the index finger's end towards the palm

		// The blade's direction in the model (from the grip towards the model's middle). Staves reach out both ways from
		// the grip and their middle is often below it (vanilla staff01: 21 units towards the butt), which turned the hand
		// upside down: their shaft is the model's +Y towards the head, like the blades of swords. (Bows keep their middle:
		// without a bow skeleton it tells the string side.)
		NiPoint3 BladeInModel()
		{
			const bool behindGrip = !state.bow && state.center.y < 1.0f;
			if (state.staff || behindGrip || state.center.Length() <= 1.0f) {
				return { 0.0f, 1.0f, 0.0f };
			}
			return state.center;
		}

		// The fist around the WEAPON node's handle (as tuned), then the player's own fit from the settings (models whose
		// grip isn't where the game expects it, e.g. held at the crossguard): tilted in the fist, turned around the blade,
		// slid along it. Every frame, so the menu's sliders show right away.
		void ApplyFist()
		{
			const auto& settings = Settings::Get();
			const float slide = state.thirdPerson ? settings.bodyGripSlide : settings.gripSlide;
			const float turn = state.thirdPerson ? settings.bodyGripTurn : settings.gripTurn;
			const float tilt = state.thirdPerson ? settings.bodyGripTilt : settings.gripTilt;
			state.grip = state.weaponNode;
			state.grip.translate += state.thirdPerson ? kBodyFistOffset : kFistOffset;
			state.grip.rotate = AxisAngle({ 0.0f, 0.0f, 1.0f }, kFistTurn * kDegrees) * AxisAngle({ 0.0f, -1.0f, 0.0f }, (kFistTilt + tilt) * kDegrees) *
			                    state.grip.rotate;
			const NiPoint3 blade = Normalized(state.grip.rotate * BladeInModel());
			// around the handle's point in the fist (the model origin), so the handle stays in the hand
			state.grip.rotate = AxisAngle(blade, turn * kDegrees) * state.grip.rotate;
			// + = the weapon moves towards its tip: the hand ends up nearer the pommel
			state.grip.translate += blade * slide;
			state.bladeAxis = blade;
		}

		// Weapons: where the game puts held weapons (the WEAPON node at the grip), the hand a fist around it. The blade
		// direction (grip towards the model's middle) is what the hand turns around.
		void SetWeaponGrip(const ArmPose::HandFrame& a_hand)
		{
			state.gripSet = true;
			if (!a_hand.weaponGrip) {
				state.weapon = false;  // no WEAPON node: hold it like other items
				return;
			}
			state.weaponNode = *a_hand.weaponGrip;
			state.weaponNode.scale *= state.scale;
			state.grip = state.weaponNode;
			if (!state.stringBow) {
				ApplyFist();
			}
			state.bladeAxis = Normalized(state.grip.rotate * BladeInModel());
			if (state.stringBow) {
				SetBowGrip(a_hand);
			}
			logs::info("Weapon grip: blade ({:.2f}, {:.2f}, {:.2f}) in hand space", state.bladeAxis.x, state.bladeAxis.y, state.bladeAxis.z);
		}

		// a node's transform in the item's model space (below the copy's root)
		std::optional<NiTransform> ModelSpace(const char* a_name)
		{
			const auto root = state.item.get();
			auto       node = root ? root->GetObjectByName(a_name) : nullptr;
			if (!node) {
				return std::nullopt;
			}
			NiTransform result;
			for (; node && node != root; node = node->parent) {
				result = Combine(node->local, result);
			}
			return result;
		}

		// Skinned bows have a skeleton: Bow_MidBone at the middle of the riser, the limb chains end in Bow_StringBone1
		// (lower) and Bow_StringBone2 (upper) where the string is tied. Model space: grip point, limbs (lower -> upper)
		// and the direction from the riser to the string.
		struct BowShape
		{
			NiPoint3 grip;
			NiPoint3 limbs;
			NiPoint3 toString;
		};

		std::optional<BowShape> MeasureBow()
		{
			const auto mid = ModelSpace("Bow_MidBone");
			const auto lower = ModelSpace("Bow_StringBone1");
			const auto upper = ModelSpace("Bow_StringBone2");
			if (!mid || !lower || !upper) {
				return std::nullopt;
			}
			const NiPoint3 along = upper->translate - lower->translate;
			if (along.Length() < 10.0f) {
				return std::nullopt;
			}
			BowShape       shape{ .grip = mid->translate, .limbs = Normalized(along) };
			const NiPoint3 toLine = (upper->translate + lower->translate) * 0.5f - mid->translate;
			const NiPoint3 across = toLine - shape.limbs * toLine.Dot(shape.limbs);
			if (across.Length() < 1.0f) {
				return std::nullopt;  // a straight stick: no telling the string side
			}
			shape.toString = Normalized(across);
			return shape;
		}

		// Like an archer's bow hand: the fist around the middle of the riser, the limbs along the knuckles (upper limb on
		// the thumb side), the string on the wrist side; the hand then holds the limbs up with the string to the eye.
		void SetBowGrip(const ArmPose::HandFrame& a_hand)
		{
			const NiPoint3 knuckles = Normalized(a_hand.fingerAxis.Cross(a_hand.palmAxis));  // towards the thumb
			if (const auto shape = MeasureBow()) {
				const NiPoint3 wrist = Normalized(-a_hand.fingerAxis - knuckles * (-a_hand.fingerAxis).Dot(knuckles));
				const NiPoint3 fist = state.grip.translate;  // where the WEAPON node puts a handle
				state.grip.rotate = AlignAxes(shape->limbs, shape->toString, knuckles, wrist);
				state.grip.translate = fist - state.grip.rotate * (shape->grip * state.grip.scale);
				state.bladeAxis = knuckles;
				state.stringAxis = wrist;
				logs::info("Bow held at its riser ({:.1f}, {:.1f}, {:.1f})", shape->grip.x, shape->grip.y, shape->grip.z);
				return;
			}
			// no bow skeleton: the model middle lies between the riser and the string (the limbs are even around the
			// grip), the limbs run through the fist
			const NiPoint3 toString = state.bladeAxis;
			NiPoint3       limbs = knuckles - toString * knuckles.Dot(toString);
			if (limbs.Length() < 0.3f) {
				limbs = a_hand.fingerAxis - toString * a_hand.fingerAxis.Dot(toString);
			}
			limbs = Normalized(limbs);
			const NiPoint3 wrist = -a_hand.fingerAxis;
			const NiPoint3 wanted = Normalized(wrist - limbs * wrist.Dot(limbs), toString);
			// both are across the limbs, so this turns the bow around them (its origin, the grip, stays in the fist)
			const NiMatrix3 turn = toString.Dot(wanted) < -0.999f ? AxisAngle(limbs, 3.14159265f) : RotationBetween(toString, wanted);
			state.grip.rotate = turn * state.grip.rotate;
			state.bladeAxis = limbs;
			state.stringAxis = wanted;
		}

		// The hand holding a weapon: blade up and forward, palm to the left (a fist around the grip), then turned by the
		// player: bent around the view's right axis, twisted around the blade
		NiMatrix3 WeaponHand(const CameraFrame& a_frame, const ArmPose::HandFrame& a_hand)
		{
			// the blade starts straight up and leans left / away from the eye by the settings' angles; the palm faces
			// left (inwards), like holding a sword up to look at it
			const auto&     settings = Settings::Get();
			const bool      body = state.thirdPerson && !state.bow;
			const float     leanLeft = state.bow ? settings.bowLeanLeft : body ? kBodyWeaponLeanLeft : settings.weaponLeanLeft;
			const float     leanForward = state.bow ? settings.bowLeanForward : body ? kBodyWeaponLeanForward : settings.weaponLeanForward;
			const float     roll = body ? kBodyWeaponRoll : settings.weaponRoll;
			const NiMatrix3 lean = AxisAngle(a_frame.forward, -leanLeft * kDegrees) * AxisAngle(a_frame.right, -leanForward * kDegrees);
			const NiPoint3  bladeWorld = Normalized(lean * a_frame.up);
			// bows: the string towards the eye and 45 degrees to the left, so it passes beside the forearm, not through it
			const NiMatrix3 aligned = state.stringBow ? AlignAxes(state.bladeAxis, state.stringAxis, bladeWorld, -a_frame.forward - a_frame.right) :
			                                            AlignAxes(state.bladeAxis, a_hand.palmAxis, bladeWorld, -a_frame.right);
			const NiMatrix3 base = AxisAngle(bladeWorld, roll * kDegrees) * aligned;
			return AxisAngle(a_frame.right, state.bend) * AxisAngle(bladeWorld, state.twist) * base;
		}

		NiTransform RestingItem(const ArmPose::Hand& a_hand, const CameraFrame& a_frame);
		NiTransform FloatingItem(const ArmPose::Hand& a_hand, const CameraFrame& a_frame);

		// item on the palm, turned the way the player turned it
		NiTransform HeldItem(const ArmPose::Hand& a_hand, const CameraFrame& a_frame)
		{
			if (state.weapon) {
				return Combine(a_hand.world, state.grip);
			}
			return state.settled ? RestingItem(a_hand, a_frame) : FloatingItem(a_hand, a_frame);
		}

		// items (not weapons) are shown smaller in 3rd person, where the camera is close to them
		float ItemSize() { return state.thirdPerson && !state.weapon ? Settings::Get().bodyItemScale : 1.0f; }

		// lying on the palm (after it came down to put it away)
		NiTransform RestingItem(const ArmPose::Hand& a_hand, const CameraFrame& a_frame)
		{
			NiTransform world;
			world.rotate = a_frame.basis * state.rotation;
			world.scale = state.scale * ItemSize();
			const float    lift = std::clamp(state.radius * ItemSize() * 0.55f, 1.5f, 6.0f) + kRestAbovePalm;
			const NiPoint3 center = a_hand.palmCenter + a_hand.palmNormal * lift;
			world.translate = center - world.rotate * (state.center * world.scale);
			return world;
		}

		// floating over the hand while it's being looked at
		NiTransform FloatingItem(const ArmPose::Hand& a_hand, const CameraFrame& a_frame)
		{
			NiTransform world;
			world.rotate = a_frame.basis * state.rotation;
			world.scale = state.scale * ItemSize();
			// floats straight up (on screen) over the palm, its lower side about holdHeight above the hand, bobbing a little
			// with telekinesis, then moved by the player's item offset (eye space: right, forward, up)
			const auto&    settings = Settings::Get();
			const float    bob = state.telekinesis ? kFloatBob * std::sin(state.clock * 2.3f) : 0.0f;
			const float    lift = std::clamp(state.radius * ItemSize() * 0.6f, 2.0f, 12.0f) + settings.holdHeight + bob;
			// 3rd person: straight out of the open palm, moved by its own offsets (the 1st person ones are for the view
			// from the eyes)
			const NiPoint3 itemOffset = state.thirdPerson ? NiPoint3{ settings.bodyItemRight, settings.bodyItemForward, settings.bodyItemUp } :
			                                                NiPoint3{ settings.itemRight, settings.itemForward, settings.itemUp };
			const NiPoint3 offset = a_frame.right * itemOffset.x + a_frame.forward * itemOffset.y + a_frame.up * itemOffset.z;
			// (leaning upwards a little, so a palm that didn't turn all the way up still has the item above it)
			const NiPoint3 out = state.thirdPerson ? Normalized(a_hand.palmNormal + NiPoint3{ 0.0f, 0.0f, 0.5f }) : a_frame.up;
			const NiPoint3 center = a_hand.palmCenter + out * lift + offset;
			world.translate = center - world.rotate * (state.center * state.scale);
			return world;
		}

		NiTransform Blend(const NiTransform& a_from, const NiTransform& a_to, float a_t)
		{
			NiTransform result;
			result.rotate = Slerp(a_from.rotate, a_to.rotate, a_t);
			result.translate = Lerp(a_from.translate, a_to.translate, a_t);
			result.scale = a_from.scale + (a_to.scale - a_from.scale) * a_t;
			return result;
		}

		// 3rd person: the pocket at the right hip and the point on the way down to it (eye space, unscaled). Standing
		// these are fixed; sneaking, the crouched hip is found from the right thigh bone.
		std::pair<NiPoint3, NiPoint3> PocketPoints(const CameraFrame& a_body, float a_scale)
		{
			const auto root = state.sneaking ? BodyRoot() : nullptr;
			const auto pelvis = root ? root->GetObjectByName("NPC Pelvis [Pelv]") : nullptr;
			const auto thigh = pelvis ? pelvis->GetObjectByName("NPC R Thigh [RThg]") : nullptr;
			if (!thigh || a_scale <= 0.0f) {
				return { kPocket, kPocketVia };
			}
			const NiPoint3 fromEye = (thigh->world.translate - a_body.eye) / a_scale;
			const NiPoint3 pocket = NiPoint3{ fromEye.Dot(a_body.right), fromEye.Dot(a_body.forward), fromEye.Dot(a_body.up) } + kPocketFromThigh;
			return { pocket, pocket + (kPocketVia - kPocket) };
		}

		// Poses the arm of a_root for this frame and places the item in the hand.
		// a_body: what the hand is held from (1st person: the camera; 3rd person: the eyes, facing like the player).
		// a_view: what the item turns in and a weapon leans in (the camera; 3rd person: where it flies to).
		// a_scale: the body's size, for the 3rd person offsets.
		void PoseArm(RE::NiNode* a_root, const CameraFrame& a_body, const CameraFrame& a_view, float a_scale)
		{
			const auto& settings = Settings::Get();
			const auto  at = [&](const NiPoint3& a_local) { return a_body.ToWorld(a_local * a_scale); };

			// hold pose: wrist low right in front of the eye, palm up, fingers forward and a bit left
			const float    sway = 0.35f * std::sin(state.clock * 1.9f);
			// weapons have their own hold position in 1st person (they're held lower and further out)
			const bool     bodyWeapon = state.thirdPerson && state.weapon && !state.bow;
			const NiPoint3 bodyHold = BodyHold();
			const NiPoint3 holdAt = bodyWeapon        ? bodyHold + kBodyWeaponOffset :
			                        state.thirdPerson ? bodyHold :
			                        state.bow         ? NiPoint3{ settings.bowRight, settings.bowForward, settings.bowUp } :
			                        state.weapon      ? NiPoint3{ settings.weaponRight, settings.weaponForward, settings.weaponUp } :
			                                            NiPoint3{ settings.holdRight, settings.holdForward, settings.holdUp };
			const NiPoint3 holdWrist = at({ holdAt.x, holdAt.y + state.distanceOffset, holdAt.z + sway });
			// the hand's own turn from the settings (turn around up, tilt around right, roll around the fingers), and the
			// wrist leaning a little into the way the item turns
			const NiPoint3  baseFingers = a_body.Direction({ -0.6f, 1.0f, 0.1f });
			const NiMatrix3 handTurn = AxisAngle(a_body.up, -settings.handTurn * kDegrees) * AxisAngle(a_body.right, settings.handTilt * kDegrees) *
			                           AxisAngle(baseFingers, settings.handRoll * kDegrees);
			const NiMatrix3 lean = AxisAngle(a_body.up, -0.2f * state.leanX) * AxisAngle(a_body.right, 0.2f * state.leanY);
			const NiPoint3  holdFingers = lean * handTurn * baseFingers;
			const NiPoint3  holdPalm = lean * handTurn * a_body.Direction({ -0.2f, -0.25f, 1.0f });
			// stow pose: fingers down, palm back
			const NiPoint3 stowFingers = a_body.Direction({ 0.1f, -0.3f, -1.0f });
			const NiPoint3 stowPalm = a_body.Direction({ -0.3f, -1.0f, 0.0f });

			const auto fingers = state.fingers;  // see UpdateFingers
			const auto scaled = [&](float a_weight) {
				auto result = fingers;
				for (auto& value : result) {
					value *= a_weight;
				}
				return result;
			};

			ArmPose::Goal goal{
				.wrist = holdWrist,
				.pole = a_body.Direction({ 0.8f, -0.3f, -1.0f }),
				.fingers = holdFingers,
				.palm = holdPalm,
				.weight = 1.0f,
				.curl = fingers,
				.weaponGrip = state.weapon,
				.hinge = state.thirdPerson,
				.open = state.thirdPerson && !state.weapon ? 1.0f : 0.0f  // 3rd person: an open palm under the floating item
			};
			if (state.thirdPerson && state.sneaking) {
				// the crouched arm: the elbow swung around the forward axis (out) and the right axis (back), bent more
				goal.pole = AxisAngle(a_body.right, -kSneakElbowBack * kDegrees) * (AxisAngle(a_body.forward, -kSneakElbowOut * kDegrees) * goal.pole);
				goal.elbowBend = kSneakElbowBend * kDegrees;
				goal.wristBend = kSneakWristBend * kDegrees;
			}
			std::optional<ArmPose::HandFrame> handFrame;
			if (state.weapon) {
				handFrame = ArmPose::Frame(a_root);
				if (handFrame && !state.gripSet) {
					SetWeaponGrip(*handFrame);
				} else if (state.gripSet && state.weapon && !state.stringBow) {
					ApplyFist();  // the grip sliders apply live
				}
				if (handFrame && state.weapon) {
					goal.hand = WeaponHand(a_view, *handFrame);
					if (state.phase == Phase::kHold || state.phase == Phase::kRaise) {
						state.holdHand = *goal.hand;
					}
				}
			}
			switch (state.phase) {
			case Phase::kRaise:
				goal.weight = Smooth(state.time / settings.raiseTime);
				goal.curl = scaled(goal.weight);
				break;
			case Phase::kStow:
			case Phase::kReturn:
				if (state.thirdPerson) {
					// a little up first, then down in an arc to the right hip, the item slips into the pocket; the return
					// lets the arm hang back into its own animation from there (the pocket is close to where it hangs)
					const float     s = state.phase == Phase::kStow ? Smooth(state.time / settings.stowTime) : 1.0f;
					const NiPoint3  holdLocal{ holdAt.x, holdAt.y + state.distanceOffset, holdAt.z };
					const NiPoint3  start = at(holdLocal);
					const NiPoint3  lift = at(holdLocal + kPocketLift);
					const auto [pocketAt, pocketVia] = PocketPoints(a_body, a_scale);
					const NiPoint3  via = at(pocketVia);
					const NiPoint3  end = at(pocketAt);
					const float     u = 1.0f - s;
					goal.wrist = start * (u * u * u) + lift * (3.0f * u * u * s) + via * (3.0f * u * s * s) + end * (s * s * s);
					const NiMatrix3 hold = AlignAxes({ 1, 0, 0 }, { 0, 1, 0 }, holdFingers, holdPalm);
					const NiMatrix3 pocket = AlignAxes({ 1, 0, 0 }, { 0, 1, 0 }, a_body.Direction(kPocketFingers), a_body.Direction(kPocketPalm));
					const NiMatrix3 turn = Slerp(hold, pocket, s * s);  // the hand keeps its hold while it rises, turns on the way down
					goal.fingers = Column(turn, 0);
					goal.palm = Column(turn, 1);
					goal.pole = Normalized(Lerp(goal.pole, a_body.Direction(kPocketElbow), s * s));
					if (goal.hand && handFrame) {  // a weapon: from the turned hold to fingers down, palm to the thigh
						goal.hand = Slerp(state.holdHand, AlignAxes(handFrame->fingerAxis, handFrame->palmAxis, Column(pocket, 0), Column(pocket, 1)), s * s);
					}
					if (state.phase == Phase::kReturn) {
						goal.weight = 1.0f - Smooth(state.time / settings.returnTime);
						goal.curl = scaled(goal.weight);
					}
					break;
				} else {
					const float s = state.phase == Phase::kStow ? Smooth(state.time / settings.stowTime) : 1.0f;
					// quadratic curve hold -> via -> end
					const NiPoint3 start = at({ holdAt.x, holdAt.y + state.distanceOffset, holdAt.z });
					const NiPoint3 via = at(kStowVia);
					const NiPoint3 end = at(kStowEnd);
					goal.wrist = start * ((1.0f - s) * (1.0f - s)) + via * (2.0f * (1.0f - s) * s) + end * (s * s);
					const NiMatrix3 hold = AlignAxes({ 1, 0, 0 }, { 0, 1, 0 }, holdFingers, holdPalm);
					const NiMatrix3 stow = AlignAxes({ 1, 0, 0 }, { 0, 1, 0 }, stowFingers, stowPalm);
					const NiMatrix3 turn = Slerp(hold, stow, s);
					goal.fingers = Column(turn, 0);
					goal.palm = Column(turn, 1);
					if (goal.hand && handFrame) {  // a weapon: from the turned hold to fingers down, palm back
						goal.hand = Slerp(state.holdHand, AlignAxes(handFrame->fingerAxis, handFrame->palmAxis, stowFingers, stowPalm), s);
					}
					if (state.phase == Phase::kReturn) {
						goal.weight = 1.0f - Smooth(state.time / settings.returnTime);
						goal.curl = scaled(goal.weight);
					}
					break;
				}
			case Phase::kPutBack:
				goal.weight = 1.0f - Smooth(state.time / settings.returnTime);
				goal.curl = scaled(goal.weight);
				break;
			default:
				break;
			}

			const auto hand = ArmPose::Apply(a_root, goal);
			if (!hand) {
				return;
			}
			// Improved Camera's 3rd person body: our 1st person arm replaces its right arm
			if (!state.thirdPerson && ArmPose::ShownAgain()) {
				BodyArm::Hide(BodyRoot());
			}
			if (state.telekinesis) {
				// the hand effect glows just over the palm, turned like the magic node it was made for
				NiTransform glow;
				glow.translate = hand->palmCenter + hand->palmNormal * kGlowAbovePalm;
				const auto magicNode = HandMagicNode();
				glow.rotate = magicNode ? magicNode->world.rotate : hand->world.rotate;
				Telekinesis::Place(glow);
			}

			const bool fromRef = state.source != Source::kInventory;
			switch (state.phase) {
			case Phase::kRaise:
				// from the world the item flies from where it lay (out of a container, from inside it); out of the
				// inventory it comes up on the palm
				PlaceItem(fromRef ? Blend(state.start, HeldItem(*hand, a_view), Smooth(state.time / settings.raiseTime)) : HeldItem(*hand, a_view));
				break;
			case Phase::kHold:
			case Phase::kBuying:
			case Phase::kSwitchView:
				PlaceItem(HeldItem(*hand, a_view));
				break;
			case Phase::kSettle:
				// comes down smoothly from where it floated onto the palm
				PlaceItem(Blend(FloatingItem(*hand, a_view), RestingItem(*hand, a_view), Smooth(state.time / kSettleTime)));
				break;
			case Phase::kStow:
				if (!state.pickedUp) {
					if (!state.inHandSet) {
						state.inHand = ToLocal(hand->world, state.last);
						state.inHandSet = true;
					}
					NiTransform carried = Combine(hand->world, state.inHand);
					if (state.thirdPerson) {
						// slips into the pocket: shrinks around its middle until it's gone
						const float    r = state.time / settings.stowTime;
						const float    left = std::max(1.0f - Smooth((r - kPocketShrinkFrom) / (kStowPickUpAt - kPocketShrinkFrom)), 0.01f);
						const NiPoint3 middle = carried.translate + carried.rotate * (state.center * carried.scale);
						carried = ItemAt(middle, carried.rotate, carried.scale * left);
					}
					PlaceItem(carried);
				}
				break;
			case Phase::kPutBack:
				{
					// from the hand back to where it lay (into the container / plant)
					const float t = Smooth(state.time / settings.returnTime);
					PlaceItem(Blend(HeldItem(*hand, a_view), state.start, t));
					break;
				}
			default:
				break;
			}
		}

		// runs after the 1st person camera update, when the animation already posed the 1st person skeleton
		void OnFirstPersonCamera()
		{
			if (state.thirdPerson) {
				return;
			}
			if (state.phase == Phase::kIdle || state.phase == Phase::kWaitMenu || state.phase == Phase::kWaitAnimation || state.phase == Phase::kFadeOut ||
				state.phase == Phase::kWaitCamera || state.phase == Phase::kFadeBack || (state.phase == Phase::kSwitchView && state.switched)) {
				BodyArm::Restore();  // our arm isn't shown now
				ShowCameraArms(false);
				return;
			}
			const auto player = Player();
			const auto root = FirstPersonRoot();
			const auto camera = RE::Main::WorldRootCamera();
			if (!player || !root || !camera) {
				return;
			}
			ShowCameraArms(true);
			const auto frame = MakeCameraFrame(camera->world.translate, player->data.angle.x, player->data.angle.z);
			PoseArm(root, frame, frame, 1.0f);
			ApplyHeadLook(frame);
		}

		bool bodyHook{ false };  // the 3rd person arm is posed right after the player's skeletons were updated

		bool PosingBody()
		{
			return state.thirdPerson && state.phase != Phase::kIdle && state.phase != Phase::kWaitMenu && state.phase != Phase::kWaitCamera &&
			       state.phase != Phase::kCameraBack && !(state.phase == Phase::kSwitchView && state.switched);
		}

		// the body's arm holds the item
		void PoseBodyArm()
		{
			const auto frames = ThirdPersonFrames();
			const auto root = BodyRoot();
			if (frames && root) {
				PoseArm(root, frames->body, frames->view, state.bodyScale);
			}
		}

		// 3rd person: the head turns to the item by the eased weight (after the arm placed the item for this frame)
		void LookAtItem()
		{
			const auto root = BodyRoot();
			if (!state.thirdPerson || !root || !state.lookTargetSet) {
				return;
			}
			const float heading = Player()->data.angle.z;
			HeadLook::Apply(root, state.lookTarget, { std::sin(heading), std::cos(heading), 0.0f }, Smooth(state.headWeight));
		}

		// runs right after the player's skeletons were animated and updated, before anything else reads the bones
		void OnPlayerSkeletons()
		{
			if (PosingBody()) {
				PoseBodyArm();
				LookAtItem();
			} else if (state.thirdPerson && state.phase == Phase::kCameraBack) {
				LookAtItem();  // whatever is left of the turn eases out
			}
		}

		// runs after the 3rd person camera update (the game has just set the camera root): the camera flies between
		// where the game had it and over the shoulder
		void OnThirdPersonCamera()
		{
			if (!state.thirdPerson || state.phase == Phase::kIdle || state.phase == Phase::kWaitMenu || state.phase == Phase::kWaitCamera) {
				return;
			}
			if (!bodyHook && PosingBody()) {
				PoseBodyArm();  // fallback when the skeleton update call wasn't found: physics may read the bones first
				LookAtItem();
			}
			const auto frames = ThirdPersonFrames();
			if (!frames) {
				return;
			}
			if (!SmoothCam::HasCamera()) {
				SmoothCam::TakeCamera();  // asked again every frame; until it's given SmoothCam moves the camera, not us
				if (!SmoothCam::HasCamera()) {
					return;
				}
			}
			if (state.captureStartOnReturn && !state.cameraIn) {
				if (const auto camera = GameCamera()) {
					state.cameraStart = camera->translate;  // the game just set it, we haven't moved it yet
					state.cameraStartAxes = CameraAxes(camera->rotate);
				}
				state.captureStartOnReturn = false;
			}
			const float s = Smooth(state.cameraBlend);
			PlaceCamera(Lerp(state.cameraStart, frames->view.eye, s), Slerp(state.cameraStartAxes, CameraAxes(frames->view), s));
			ApplyHeadLook(frames->view);
		}

		// ---- hooks ----

		struct PlayerUpdate
		{
			static void thunk(RE::PlayerCharacter* a_this, float a_delta)
			{
				func(a_this, a_delta);
				OnPlayerUpdate(a_delta);
			}
			static inline REL::Relocation<decltype(thunk)> func;
		};

		// The player's animation update (the 3rd person behavior graph): slowed down to a stop while the body holds still
		struct PlayerAnimation
		{
			static void thunk(RE::PlayerCharacter* a_this, float a_delta)
			{
				func(a_this, state.thirdPerson ? a_delta * (1.0f - state.stillness) : a_delta);
			}
			static inline REL::Relocation<decltype(thunk)> func;
		};

		struct FirstPersonUpdate
		{
			static void thunk(RE::TESCameraState* a_this, RE::BSTSmartPointer<RE::TESCameraState>& a_next)
			{
				func(a_this, a_next);
				OnFirstPersonCamera();
			}
			static inline REL::Relocation<decltype(thunk)> func;
		};

		struct ThirdPersonUpdate
		{
			static void thunk(RE::TESCameraState* a_this, RE::BSTSmartPointer<RE::TESCameraState>& a_next)
			{
				func(a_this, a_next);
				OnThirdPersonCamera();
			}
			static inline REL::Relocation<decltype(thunk)> func;
		};

		// The player's 3D update (SE 39446 / AE 40522) animates and updates the 3rd person body, then calls
		// NiAVObject::Update on the 1st person skeleton (+0xD7, this call). The 3rd person arm is posed right after it,
		// before the camera, physics (HDT-SMP) or other mods read the bones of this frame. Improved Camera hooks the
		// same call for its 1st person body; both calls chain.
		struct PlayerSkeletonsUpdate
		{
			static void thunk(RE::NiAVObject* a_object, RE::NiUpdateData* a_data)
			{
				func(a_object, a_data);
				OnPlayerSkeletons();
			}
			static inline REL::Relocation<decltype(thunk)> func;
		};

		void InstallPlayerSkeletonsHook()
		{
			const auto address = REL::RelocationID(39446, 40522).address() + 0xD7;
			if (*reinterpret_cast<const std::uint8_t*>(address) != 0xE8) {
				logs::warn("Player skeleton update call not found: the 3rd person arm is posed from the camera update");
				return;
			}
			PlayerSkeletonsUpdate::func = SKSE::GetTrampoline().write_call<5>(address, PlayerSkeletonsUpdate::thunk);
			bodyHook = true;
		}

		class InputSink : public RE::BSTEventSink<RE::InputEvent*>
		{
		public:
			static InputSink* GetSingleton()
			{
				static InputSink sink;
				return &sink;
			}

			// "Hold a key to inspect": follows the key, and remembers for each Activate press whether it was held
			static void TrackInspectKey(RE::InputEvent* a_events)
			{
				const auto& settings = Settings::Get();
				if (!settings.holdKeyToInspect) {
					return;
				}
				const auto userEvents = RE::UserEvents::GetSingleton();
				for (auto event = a_events; event; event = event->next) {
					const auto button = event->GetEventType() == RE::INPUT_EVENT_TYPE::kButton ? event->AsButtonEvent() : nullptr;
					if (!button) {
						continue;
					}
					const int code = Keys::Code(button);
					if (code != Keys::kNone && (code == settings.inspectKey || code == settings.inspectGamepadKey)) {
						inspectKeyHeld = button->IsPressed();
					} else if (button->IsDown() && userEvents && button->QUserEvent() == userEvents->activate) {
						activatedWithKey = inspectKeyHeld.load();
					}
				}
			}

			RE::BSEventNotifyControl ProcessEvent(RE::InputEvent* const* a_event, RE::BSTEventSource<RE::InputEvent*>*) override
			{
				if (a_event) {
					TrackInspectKey(*a_event);
				}
				if (!a_event || !Active() || state.phase == Phase::kWaitMenu || state.phase == Phase::kWaitAnimation) {
					return RE::BSEventNotifyControl::kContinue;
				}
				const auto& settings = Settings::Get();
				for (auto event = *a_event; event; event = event->next) {
					switch (event->GetEventType()) {
					case RE::INPUT_EVENT_TYPE::kButton:
						{
							const auto button = event->AsButtonEvent();
							const int  look = Keys::Code(button);
							if (look != Keys::kNone && (look == settings.lookKey || look == settings.lookGamepadKey)) {
								// held: look around; released: the camera turns back
								const bool canLook = (state.phase == Phase::kRaise || state.phase == Phase::kHold) && state.clock > kInputDelay;
								if (button->IsPressed() && canLook) {
									StartLooking();
								} else if (!button->IsPressed()) {
									StopLooking(false);
								}
								break;
							}
							if (!button->IsDown()) {
								break;
							}
							if (button->GetDevice() == RE::INPUT_DEVICE::kMouse &&
								(button->GetIDCode() == RE::BSWin32MouseDevice::Key::kWheelUp || button->GetIDCode() == RE::BSWin32MouseDevice::Key::kWheelDown)) {
								state.wheel += button->GetIDCode() == RE::BSWin32MouseDevice::Key::kWheelUp ? 1.0f : -1.0f;
								break;
							}
							const int code = Keys::Code(button);
							// the key press that picked the item up may come through here too
							const bool accept = (state.phase == Phase::kRaise || state.phase == Phase::kHold) && state.clock > kInputDelay;
							if (code == Keys::kNone || !accept) {
								break;
							}
							if (code == settings.switchViewKey || code == settings.switchViewGamepadKey) {
								state.switchRequested = state.phase == Phase::kHold;
							} else if (code == settings.storeKey || code == settings.storeGamepadKey) {
								state.storeRequested = true;
							} else if (code == settings.putBackKey || code == settings.putBackGamepadKey) {
								state.putBackRequested = true;
							}
							break;
						}
					case RE::INPUT_EVENT_TYPE::kMouseMove:
						{
							const auto move = static_cast<RE::MouseMoveEvent*>(event);
							state.mouseX += static_cast<float>(move->mouseInputX);
							state.mouseY += static_cast<float>(move->mouseInputY);
							break;
						}
					case RE::INPUT_EVENT_TYPE::kThumbstick:
						{
							const auto stick = static_cast<RE::ThumbstickEvent*>(event);
							if (stick->IsRight()) {
								state.stickX = stick->xValue;
								state.stickY = stick->yValue;
							}
							break;
						}
					default:
						break;
					}
				}
				return RE::BSEventNotifyControl::kContinue;
			}
		};

		// Calls of Inventory3DManager::ToggleItemZoom (AE 51760) in the inventory menu: its message handler ("Item Zoom"
		// user event, AE 51848) and its zoom callback (AE 51862). Found in 1.7.104; the bytes are checked before patching.
		struct CallSite
		{
			std::uint64_t id;
			std::ptrdiff_t offset;
		};
		constexpr std::array<CallSite, 3> kItemZoomCalls{ { { 51848, 0x406 }, { 51862, 0x94 }, { 51862, 0xE6 } } };

		void InstallItemZoomHook()
		{
			if (!REL::Module::IsAE()) {
				logs::warn("Taking items out of the inventory is only supported on Skyrim AE");
				return;
			}
			const auto target = REL::ID(51760).address();
			auto&      trampoline = SKSE::GetTrampoline();
			int        installed = 0;
			for (const auto& site : kItemZoomCalls) {
				const auto address = REL::ID(site.id).address() + site.offset;
				const auto bytes = reinterpret_cast<const std::uint8_t*>(address);
				const auto destination = address + 5 + *reinterpret_cast<const std::int32_t*>(address + 1);
				if (bytes[0] != 0xE8 || destination != target) {
					logs::warn("Inventory zoom call {} +0x{:X} not found, skipped", site.id, site.offset);
					continue;
				}
				ItemZoom::func = trampoline.write_call<5>(address, ItemZoom::thunk);
				++installed;
			}
			logs::info("Hooked {} of {} inventory item zoom calls", installed, kItemZoomCalls.size());
		}
	}

	void InstallHooks()
	{
		REL::Relocation<std::uintptr_t> player{ RE::VTABLE_PlayerCharacter[0] };
		PlayerUpdate::func = player.write_vfunc(0xAD, PlayerUpdate::thunk);
		PlayerAnimation::func = player.write_vfunc(0x7D, PlayerAnimation::thunk);
		PickUpObject::func = player.write_vfunc(0xCC, PickUpObject::thunk);
		REL::Relocation<std::uintptr_t> firstPerson{ RE::VTABLE_FirstPersonState[0] };
		FirstPersonUpdate::func = firstPerson.write_vfunc(0x3, FirstPersonUpdate::thunk);
		REL::Relocation<std::uintptr_t> thirdPerson{ RE::VTABLE_ThirdPersonState[0] };
		ThirdPersonUpdate::func = thirdPerson.write_vfunc(0x3, ThirdPersonUpdate::thunk);
		REL::Relocation<std::uintptr_t> flora{ RE::VTABLE_TESFlora[0] };
		HarvestActivate<RE::TESFlora>::func = flora.write_vfunc(0x37, HarvestActivate<RE::TESFlora>::thunk);
		REL::Relocation<std::uintptr_t> tree{ RE::VTABLE_TESObjectTREE[0] };
		HarvestActivate<RE::TESObjectTREE>::func = tree.write_vfunc(0x37, HarvestActivate<RE::TESObjectTREE>::thunk);
		InstallPlayerSkeletonsHook();
		logs::info("Installed hooks: PlayerCharacter::Update, PlayerCharacter::UpdateAnimation, PlayerCharacter::PickUpObject, FirstPersonState::Update, ThirdPersonState::Update, TESFlora / TESObjectTREE::Activate{}",
			bodyHook ? ", player skeleton update" : "");
		InstallItemZoomHook();
		REL::Relocation<std::uintptr_t> entry{ RE::VTABLE_BGSEntryPointPerkEntry[0] };
		EntryConditions::func = entry.write_vfunc(0x0, EntryConditions::thunk);
		logs::info("Hooked BGSEntryPointPerkEntry::CheckConditionFilters (items for sale)");
		ContainerLid::Install();
	}

	void RegisterInput()
	{
		if (const auto input = RE::BSInputDeviceManager::GetSingleton()) {
			input->AddEventSink(InputSink::GetSingleton());
			logs::info("Listening to input");
		}
	}

	void Reset()
	{
		// the 3D is reloaded with the game: never touch the old nodes, just let go of them
		ArmPose::Forget();
		HeadLook::Forget();
		ContainerLid::Forget();
		searchedContainer = 0;
		forSaleRef = {};
		forSaleAsked = {};
		ForgetCameraArms();
		BodyArm::Forget();
		Telekinesis::Forget();
		if (!Active()) {
			return;
		}
		logs::info("Inspection dropped (game load)");
		if (state.source == Source::kContainer) {
			QuickLoot::Pause(false);
		}
		SmoothCam::GiveBack();
		Fade::Reset();
		WorldPause::Reset();
		RestoreControls();
		state = {};
	}

	bool OfferContainerItem(const ContainerItem& a_item)
	{
		if (!CanInspectContainerItem(a_item)) {
			return false;
		}
		containerPending = true;
		SKSE::GetTaskInterface()->AddTask([a_item]() {
			containerPending = false;
			if (!BeginFromContainer(a_item)) {
				GiveContainerItem(a_item);  // QuickLoot left it to us: taken the normal way
			}
		});
		return true;
	}

	void ContainerShown(RE::ObjectRefHandle a_container)
	{
		if (a_container.native_handle() != searchedContainer.load()) {
			searchedContainer = 0;
		}
	}
}
