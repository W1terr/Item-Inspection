#include "Inspect.h"

#include "ArmPose.h"
#include "BodyArm.h"
#include "Fade.h"
#include "HUD.h"
#include "Keys.h"
#include "Lang.h"
#include "MathUtil.h"
#include "Seen.h"
#include "Settings.h"
#include "Telekinesis.h"
#include "WorldPause.h"

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
			kFadeOut,     // from 3rd person: fading to black before the switch to 1st person
			kWaitCamera,  // switching to 1st person
			kRaise,       // hand brings the item up
			kHold,        // looking at it, mouse turns it
			kSettle,      // a floating item sinks onto the palm before it's put away / back
			kStow,        // hand goes down and behind the back, item goes into the inventory
			kReturn,      // empty hand comes back to the normal pose
			kPutBack,     // item goes back where it was, hand lowers
			kFadeBack     // back to 3rd person: fading to black before the switch
		};

		enum class Source
		{
			kWorld,     // taken from the world: picked up when it goes into the backpack
			kInventory  // already ours, taken out of the inventory menu: the menu opens again at the end
		};

		// wrist positions while putting the item away, eye space (right, forward, up): out of view, low and behind
		const NiPoint3 kStowVia{ 16.0f, 12.0f, -28.0f };
		const NiPoint3 kStowEnd{ 20.0f, -6.0f, -38.0f };
		constexpr float kStowPickUpAt = 0.8f;  // part of the stow time after which the item is in the backpack
		constexpr float kCameraTimeout = 2.0f;
		constexpr float kCurl = 0.2f;          // finger bend while holding, radians per joint
		constexpr float kOpenCurl = 0.03f;     // telekinesis: open hand
		constexpr float kFloatBob = 0.5f;      // telekinesis: the item bobs this much (units) over the palm
		constexpr float kActivityRise = 6.0f;    // 1/s, how fast the hand's activity follows the turn speed up
		constexpr float kActivityFall = 1.5f;    // ... and settles when the turning stops
		constexpr float kLeanFollow = 3.0f;      // 1/s, wrist lean
		constexpr float kFingerFollow = 6.0f;    // 1/s, how fast each finger follows its target bend
		constexpr float kMaxFingerWave = 3.5f;   // radians per second the finger wave runs at most while the item turns
		constexpr float kSettleTime = 0.3f;      // seconds the item takes to sink onto the palm
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

			// the pickup we hold back
			RE::ObjectRefHandle           ref;
			RE::FormID                    baseID{ 0 };
			std::int32_t                  count{ 1 };
			bool                          arg3{ false };
			bool                          playSound{ true };
			bool                          pickedUp{ false };

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
			bool                          bow{ false };  // bows and crossbows: their own hold settings (held further out)
			bool                          gripSet{ false };
			NiTransform                   grip;
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
			text += state.source == Source::kWorld ? std::format("[{}] {}    [{}] {}", store, Lang::T("Put in backpack"), putBack, Lang::T("Put back")) :
			                                         std::format("[{}] {}", store, Lang::T("Put in backpack"));
			HUD::ShowHint(text);  // stays until the item is put away
		}

		// ---- controls ----

		void DisableControls()
		{
			const auto controls = RE::ControlMap::GetSingleton();
			if (!controls) {
				return;
			}
			for (const auto flag : { Flag::kMovement, Flag::kLooking, Flag::kActivate, Flag::kMenu, Flag::kPOVSwitch, Flag::kFighting,
					 Flag::kSneaking, Flag::kWheelZoom, Flag::kJumping }) {
				if (controls->AreControlsEnabled(flag)) {
					controls->ToggleControls(flag, false, false);
					state.disabledControls.push_back(flag);
				}
			}
			HUD::SetHoldMode(true);  // no crosshair item info, compass or bars while holding
			state.hudModePushed = true;
		}

		void RestoreControls()
		{
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

		RE::NiNode* FirstPersonRoot()
		{
			const auto player = Player();
			const auto root = player ? player->Get3D(true) : nullptr;
			return root ? root->AsNode() : nullptr;
		}

		void PlaceItem(const NiTransform& a_world)
		{
			const auto root = FirstPersonRoot();
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
		}

		void HideWeapon()
		{
			const auto player = Player();
			const auto root = FirstPersonRoot();
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

		void Finish()
		{
			StopLooking(true);
			WorldPause::End();
			Telekinesis::Stop();
			ArmPose::Release();
			BodyArm::Restore();
			DetachItem();
			ShowWorldModel();
			RestoreControls();
			if (state.hiddenWeapon) {
				state.hiddenWeapon->SetAppCulled(false);
			}
			if (state.wasThirdPerson) {
				if (const auto camera = RE::PlayerCamera::GetSingleton(); camera && camera->IsInFirstPerson()) {
					camera->ForceThirdPerson();
				}
			}
			Fade::To(0.0f, Settings::Get().fadeTime * 1.5f);  // after a fade to black: show the world again
			const bool reopen = state.source == Source::kInventory && state.phase != Phase::kWaitMenu;
			state = {};
			if (reopen) {
				ShowInventory(true);
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

		bool CanInspect(RE::PlayerCharacter* a_player, RE::TESObjectREFR* a_ref)
		{
			const auto& settings = Settings::Get();
			if (!settings.enabled || Active() || !a_ref || a_ref->IsDisabled() || a_ref->IsMarkedForDeletion() || !a_ref->Get3D()) {
				return false;
			}
			const auto base = a_ref->GetBaseObject();
			if (!base || !KindInspected(base)) {
				return false;
			}
			if (!settings.alwaysInspect && Seen::Contains(base->GetFormID())) {
				return false;  // looked at this kind of item before
			}
			if (!settings.inspectStolen && a_ref->IsCrimeToActivate()) {
				return false;  // stealing: normal pickup
			}
			if (!PlayerFree(a_player) || (settings.skipInCombat && a_player->IsInCombat()) || MenuOpen()) {
				return false;
			}
			return true;
		}

		// to 1st person and no walking / looking around while the hand holds the item
		void TakeControl()
		{
			const auto& settings = Settings::Get();
			DisableControls();
			if (settings.worldWaits) {
				WorldPause::Begin();
			}
			const auto camera = RE::PlayerCamera::GetSingleton();
			if (!camera->IsInFirstPerson()) {
				state.wasThirdPerson = true;
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

		bool Begin(RE::PlayerCharacter* a_player, RE::TESObjectREFR* a_ref, std::int32_t a_count, bool a_arg3, bool a_playSound)
		{
			if (!CanInspect(a_player, a_ref)) {
				return false;
			}
			const auto model = a_ref->Get3D();
			state.weapon = a_ref->GetBaseObject()->IsWeapon();
			if (const auto weapon = a_ref->GetBaseObject()->As<RE::TESObjectWEAP>()) {
				state.bow = weapon->IsBow() || weapon->IsCrossbow();
				state.stringBow = weapon->IsBow();
			}
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
			model->SetAppCulled(true);
			TakeControl();
			logs::info("Inspecting {} ({:08X}, x{}), size {:.1f}, scale {:.2f}", state.name, a_ref->GetFormID(), a_count, state.radius, state.scale);
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
			const auto named = selected ? selected->As<RE::TESFullName>() : nullptr;
			state.name = named && named->GetFullName() ? named->GetFullName() : "";
			// the inventory camera looks along +Y with +Z up: keep the turn the preview had (a model we loaded starts upright)
			state.rotation = preview ? model->world.rotate : NiMatrix3{};
			SetPhase(Phase::kWaitMenu);
			CloseInventoryMenus();
			logs::info("Inspecting {} ({:08X}) from the inventory ({} model), size {:.1f}, scale {:.2f}", state.name, state.baseID,
				preview ? "preview" : "loaded", state.radius, state.scale);
			return true;
		}

		void PickUpObject::thunk(RE::PlayerCharacter* a_this, RE::TESObjectREFR* a_object, std::int32_t a_count, bool a_arg3, bool a_playSound)
		{
			if (a_this == Player() && Begin(a_this, a_object, a_count, a_arg3, a_playSound)) {
				return;  // picked up later, when it goes into the backpack
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
			if (state.settled || state.phase == Phase::kSettle) {
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
			const float follow = std::min(1.0f, a_delta * kFingerFollow);
			for (int i = 0; i < 5; ++i) {
				state.fingers[i] += (target[i] - state.fingers[i]) * follow;
			}
		}

		RE::NiAVObject* HandMagicNode()
		{
			const auto root = FirstPersonRoot();
			return root ? root->GetObjectByName("NPC R MagicNode [RMag]") : nullptr;
		}

		// the telekinesis grab sound, for items that float (picked up, put in the backpack, put back down)
		void PlayTelekinesisSound(RE::NiAVObject* a_node)
		{
			if (state.telekinesis && Settings::Get().telekinesisSound) {
				Telekinesis::PlayGrabSound(a_node);
			}
		}

		void ClearInput()
		{
			state.mouseX = state.mouseY = state.wheel = 0.0f;
		}

		void OnPlayerUpdate(float a_delta)
		{
			Telekinesis::Update(a_delta);
			Fade::Update(a_delta);
			if (!Active()) {
				return;
			}
			const auto& settings = Settings::Get();
			const auto  player = Player();
			const bool  world = state.source == Source::kWorld;
			const auto  ref = state.ref.get();
			state.time += a_delta;
			state.clock += a_delta;
			if (state.hudModePushed) {
				HUD::KeepHoldMode();
			}

			if (!player || player->IsDead() || (world && !state.pickedUp && !ref)) {
				logs::info("Inspection ended: {}", player && !player->IsDead() ? "the item is gone" : "the player died");
				Finish();
				return;
			}
			if (state.phase != Phase::kWaitCamera && state.phase != Phase::kWaitMenu && state.phase != Phase::kFadeOut && !FirstPerson()) {
				// something took the camera away: give up, the item stays where it was
				logs::info("Inspection ended: the camera left 1st person");
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
			case Phase::kWaitMenu:
				if (!InventoryMenusOpen()) {
					TakeControl();
				} else if (state.time > kCameraTimeout) {
					logs::warn("The inventory / Tab menu didn't close");
					Finish();
				}
				break;
			case Phase::kWaitCamera:
				if (FirstPerson() && FirstPersonRoot()) {
					if (world) {
						// the item keeps the turn it lay with
						const auto frame = MakeCameraFrame(state.start.translate, player->data.angle.x, player->data.angle.z);
						state.rotation = frame.basis.Transpose() * state.start.rotate;
					}
					HideWeapon();
					Fade::To(0.0f, settings.fadeTime * 1.5f);  // the hand is already coming up when the screen clears
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
					}
				}
				break;
			case Phase::kRaise:
				if (state.time >= settings.raiseTime) {
					SetPhase(Phase::kHold);
				}
				break;
			case Phase::kHold:
				ApplyRotationInput(a_delta);
				if (state.storeRequested || state.putBackRequested) {
					// an inventory item has nowhere else to go than the backpack
					const Phase next = state.storeRequested || !world ? Phase::kStow : Phase::kPutBack;
					StopLooking(false);
					state.inHandSet = false;
					if (state.weapon || next == Phase::kPutBack) {
						// a weapon is already in the hand; put back, the item floats straight back down (telekinesis
						// keeps it until it lies there)
						SetPhase(next);
					} else {
						state.afterSettle = next;
						SetPhase(Phase::kSettle);
					}
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
				if (!state.pickedUp && state.time >= settings.stowTime * kStowPickUpAt) {
					state.pickedUp = true;
					DetachItem();
					if (world) {
						auto hidden = state.worldModel;
						state.worldModel.reset();
						PickUpNow(state.ref, state.count, state.arg3, state.playSound, hidden);
						Seen::Add(state.baseID);
					}
					// from the player's body, not the 1st person hand: the hand is on its way behind the back and the camera
					// goes back to 3rd person right after, and a sound following the hand jumped with it (broken sound)
					PlayTelekinesisSound(player->Get3D(false));
					logs::info("Put in the backpack");
				}
				if (state.time >= settings.stowTime) {
					SetPhase(Phase::kReturn);
				}
				break;
			case Phase::kReturn:
				if (state.time >= settings.returnTime) {
					End();
				}
				break;
			case Phase::kPutBack:
				if (state.time >= settings.returnTime) {
					const auto landed = state.ref.get();
					PlayTelekinesisSound(landed && landed->Get3D() ? landed->Get3D() : player->Get3D(false));  // where it lands
					logs::info("Put back");
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
		constexpr float kFistTilt = 18.0f;                    // degrees around -Y: the index finger's end towards the fingers
		constexpr float kFistTurn = -4.0f;                    // degrees around +Z: the index finger's end towards the palm

		// Weapons: where the game puts held weapons (the WEAPON node at the grip), the hand a fist around it. The blade
		// direction (grip towards the model's middle) is what the hand turns around.
		void SetWeaponGrip(const ArmPose::HandFrame& a_hand)
		{
			state.gripSet = true;
			if (!a_hand.weaponGrip) {
				state.weapon = false;  // no WEAPON node: hold it like other items
				return;
			}
			state.grip = *a_hand.weaponGrip;
			state.grip.scale *= state.scale;
			if (!state.stringBow) {
				state.grip.translate += kFistOffset;
				state.grip.rotate = AxisAngle({ 0.0f, 0.0f, 1.0f }, kFistTurn * kDegrees) * AxisAngle({ 0.0f, -1.0f, 0.0f }, kFistTilt * kDegrees) *
				                    state.grip.rotate;
			}
			const NiPoint3 bladeModel = state.center.Length() > 1.0f ? state.center : NiPoint3{ 0.0f, 1.0f, 0.0f };
			state.bladeAxis = Normalized(state.grip.rotate * bladeModel);
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
			const float     leanLeft = state.bow ? settings.bowLeanLeft : settings.weaponLeanLeft;
			const float     leanForward = state.bow ? settings.bowLeanForward : settings.weaponLeanForward;
			const NiMatrix3 lean = AxisAngle(a_frame.forward, -leanLeft * kDegrees) * AxisAngle(a_frame.right, -leanForward * kDegrees);
			const NiPoint3  bladeWorld = Normalized(lean * a_frame.up);
			// bows: the string towards the eye and 45 degrees to the left, so it passes beside the forearm, not through it
			const NiMatrix3 aligned = state.stringBow ? AlignAxes(state.bladeAxis, state.stringAxis, bladeWorld, -a_frame.forward - a_frame.right) :
			                                            AlignAxes(state.bladeAxis, a_hand.palmAxis, bladeWorld, -a_frame.right);
			const NiMatrix3 base = AxisAngle(bladeWorld, settings.weaponRoll * kDegrees) * aligned;
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

		// lying on the palm (after it came down to put it away)
		NiTransform RestingItem(const ArmPose::Hand& a_hand, const CameraFrame& a_frame)
		{
			NiTransform world;
			world.rotate = a_frame.basis * state.rotation;
			world.scale = state.scale;
			const float    lift = std::clamp(state.radius * 0.55f, 1.5f, 6.0f) + kRestAbovePalm;
			const NiPoint3 center = a_hand.palmCenter + a_hand.palmNormal * lift;
			world.translate = center - world.rotate * (state.center * state.scale);
			return world;
		}

		// floating over the hand while it's being looked at
		NiTransform FloatingItem(const ArmPose::Hand& a_hand, const CameraFrame& a_frame)
		{
			NiTransform world;
			world.rotate = a_frame.basis * state.rotation;
			world.scale = state.scale;
			// floats straight up (on screen) over the palm, its lower side about holdHeight above the hand, bobbing a little
			// with telekinesis, then moved by the player's item offset (eye space: right, forward, up)
			const auto&    settings = Settings::Get();
			const float    bob = state.telekinesis ? kFloatBob * std::sin(state.clock * 2.3f) : 0.0f;
			const float    lift = std::clamp(state.radius * 0.6f, 2.0f, 12.0f) + settings.holdHeight + bob;
			const NiPoint3 offset = a_frame.right * settings.itemRight + a_frame.forward * settings.itemForward + a_frame.up * settings.itemUp;
			const NiPoint3 center = a_hand.palmCenter + a_frame.up * lift + offset;
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

		// runs after the camera update, when the animation already posed the 1st person skeleton
		void OnFirstPersonCamera()
		{
			if (state.phase == Phase::kIdle || state.phase == Phase::kWaitMenu || state.phase == Phase::kFadeOut || state.phase == Phase::kWaitCamera ||
				state.phase == Phase::kFadeBack) {
				BodyArm::Restore();  // our arm isn't shown now
				return;
			}
			const auto player = Player();
			const auto root = FirstPersonRoot();
			const auto camera = RE::Main::WorldRootCamera();
			if (!player || !root || !camera) {
				return;
			}
			const auto& settings = Settings::Get();
			const auto  frame = MakeCameraFrame(camera->world.translate, player->data.angle.x, player->data.angle.z);

			// hold pose: wrist low right in front of the eye, palm up, fingers forward and a bit left
			const float    sway = 0.35f * std::sin(state.clock * 1.9f);
			// weapons have their own hold position (they're held lower and further out)
			const NiPoint3 holdAt = state.bow    ? NiPoint3{ settings.bowRight, settings.bowForward, settings.bowUp } :
			                        state.weapon ? NiPoint3{ settings.weaponRight, settings.weaponForward, settings.weaponUp } :
			                                       NiPoint3{ settings.holdRight, settings.holdForward, settings.holdUp };
			const NiPoint3 holdWrist = frame.ToWorld({ holdAt.x, holdAt.y + state.distanceOffset, holdAt.z + sway });
			// the hand's own turn from the settings (turn around up, tilt around right, roll around the fingers), and the
			// wrist leaning a little into the way the item turns
			const NiPoint3  baseFingers = frame.Direction({ -0.6f, 1.0f, 0.1f });
			const NiMatrix3 handTurn = AxisAngle(frame.up, -settings.handTurn * kDegrees) * AxisAngle(frame.right, settings.handTilt * kDegrees) *
			                           AxisAngle(baseFingers, settings.handRoll * kDegrees);
			const NiMatrix3 lean = AxisAngle(frame.up, -0.2f * state.leanX) * AxisAngle(frame.right, 0.2f * state.leanY);
			const NiPoint3  holdFingers = lean * handTurn * baseFingers;
			const NiPoint3  holdPalm = lean * handTurn * frame.Direction({ -0.2f, -0.25f, 1.0f });
			// stow pose: fingers down, palm back
			const NiPoint3 stowFingers = frame.Direction({ 0.1f, -0.3f, -1.0f });
			const NiPoint3 stowPalm = frame.Direction({ -0.3f, -1.0f, 0.0f });

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
				.pole = frame.Direction({ 0.8f, -0.3f, -1.0f }),
				.fingers = holdFingers,
				.palm = holdPalm,
				.weight = 1.0f,
				.curl = fingers,
				.weaponGrip = state.weapon
			};
			std::optional<ArmPose::HandFrame> handFrame;
			if (state.weapon) {
				handFrame = ArmPose::Frame(root);
				if (handFrame && !state.gripSet) {
					SetWeaponGrip(*handFrame);
				}
				if (handFrame && state.weapon) {
					goal.hand = WeaponHand(frame, *handFrame);
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
				{
					const float s = state.phase == Phase::kStow ? Smooth(state.time / settings.stowTime) : 1.0f;
					// quadratic curve hold -> via -> end
					const NiPoint3 start = frame.ToWorld({ holdAt.x, holdAt.y + state.distanceOffset, holdAt.z });
					const NiPoint3 via = frame.ToWorld(kStowVia);
					const NiPoint3 end = frame.ToWorld(kStowEnd);
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

			const auto hand = ArmPose::Apply(root, goal);
			if (!hand) {
				return;
			}
			// Improved Camera's 3rd person body: our 1st person arm replaces its right arm
			if (ArmPose::ShownAgain()) {
				BodyArm::Hide(player->Get3D(false));
			}
			if (state.telekinesis) {
				// the hand effect glows just over the palm, turned like the magic node it was made for
				NiTransform glow;
				glow.translate = hand->palmCenter + hand->palmNormal * kGlowAbovePalm;
				const auto magicNode = HandMagicNode();
				glow.rotate = magicNode ? magicNode->world.rotate : hand->world.rotate;
				Telekinesis::Place(glow);
			}

			const bool world = state.source == Source::kWorld;
			switch (state.phase) {
			case Phase::kRaise:
				// from the world the item flies from where it lay; out of the inventory it comes up on the palm
				PlaceItem(world ? Blend(state.start, HeldItem(*hand, frame), Smooth(state.time / settings.raiseTime)) : HeldItem(*hand, frame));
				break;
			case Phase::kHold:
				PlaceItem(HeldItem(*hand, frame));
				break;
			case Phase::kSettle:
				// comes down smoothly from where it floated onto the palm
				PlaceItem(Blend(FloatingItem(*hand, frame), RestingItem(*hand, frame), Smooth(state.time / kSettleTime)));
				break;
			case Phase::kStow:
				if (!state.pickedUp) {
					if (!state.inHandSet) {
						state.inHand = ToLocal(hand->world, state.last);
						state.inHandSet = true;
					}
					PlaceItem(Combine(hand->world, state.inHand));
				}
				break;
			case Phase::kPutBack:
				{
					// from the hand back to where it lay
					const float t = Smooth(state.time / settings.returnTime);
					PlaceItem(Blend(HeldItem(*hand, frame), state.start, t));
					break;
				}
			default:
				break;
			}
			ApplyHeadLook(frame);
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

		struct FirstPersonUpdate
		{
			static void thunk(RE::TESCameraState* a_this, RE::BSTSmartPointer<RE::TESCameraState>& a_next)
			{
				func(a_this, a_next);
				OnFirstPersonCamera();
			}
			static inline REL::Relocation<decltype(thunk)> func;
		};

		class InputSink : public RE::BSTEventSink<RE::InputEvent*>
		{
		public:
			static InputSink* GetSingleton()
			{
				static InputSink sink;
				return &sink;
			}

			RE::BSEventNotifyControl ProcessEvent(RE::InputEvent* const* a_event, RE::BSTEventSource<RE::InputEvent*>*) override
			{
				if (!a_event || !Active() || state.phase == Phase::kWaitMenu) {
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
							if (code == settings.storeKey || code == settings.storeGamepadKey) {
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
		PickUpObject::func = player.write_vfunc(0xCC, PickUpObject::thunk);
		REL::Relocation<std::uintptr_t> firstPerson{ RE::VTABLE_FirstPersonState[0] };
		FirstPersonUpdate::func = firstPerson.write_vfunc(0x3, FirstPersonUpdate::thunk);
		logs::info("Installed hooks: PlayerCharacter::Update, PlayerCharacter::PickUpObject, FirstPersonState::Update");
		InstallItemZoomHook();
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
		BodyArm::Forget();
		Telekinesis::Forget();
		if (!Active()) {
			return;
		}
		logs::info("Inspection dropped (game load)");
		Fade::Reset();
		WorldPause::Reset();
		RestoreControls();
		state = {};
	}
}
