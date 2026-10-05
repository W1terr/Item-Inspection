#include "ArmPose.h"

#include "MathUtil.h"

namespace ArmPose
{
	using namespace MathUtil;

	namespace
	{
		constexpr int kFingers = 5;   // 0 = thumb
		constexpr int kJoints = 3;

		enum Bone : std::size_t
		{
			kUpper,
			kFore,
			kHand,
			kTwist1,  // forearm twist next to the hand
			kTwist2,  // forearm twist next to the elbow
			kFirstFinger,
			kBoneCount = kFirstFinger + kFingers * kJoints
		};

		constexpr std::size_t FingerBone(int a_finger, int a_joint) { return kFirstFinger + a_finger * kJoints + a_joint; }

		// the bone a bone hangs from (kBoneCount for the upper arm: its parent isn't posed)
		constexpr std::size_t ChainParent(std::size_t a_bone)
		{
			switch (a_bone) {
			case kUpper:
				return kBoneCount;
			case kFore:
				return kUpper;
			case kHand:
			case kTwist1:
			case kTwist2:
				return kFore;
			default:
				return (a_bone - kFirstFinger) % kJoints == 0 ? kHand : a_bone - 1;
			}
		}

		constexpr float kThumbCurl = 0.3f;     // the thumb bends around the fingers' axis, so only a part of the curl
		constexpr float kHiddenScale = 0.01f;  // an upper arm scaled below this was hidden by another mod

		// The fingers of a hand holding a one-handed weapon: their local rotations (w, x, y, z), taken from the first frame
		// of the 1st person one-handed idle (meshes\actors\character\_1stperson\animations\1hm_idle.hkx), thumb first,
		// 3 joints each. The weapon is fitted into this fist in Inspect (kFistOffset / kFistTilt / kFistTurn).
		constexpr std::array<MathUtil::Quat, kFingers * kJoints> kWeaponGrip{ {
			{ 0.6368f, 0.0839f, 0.5848f, -0.4954f }, { 0.8038f, 0.5897f, -0.0632f, 0.0463f }, { 0.9072f, 0.4201f, -0.0221f, 0.0100f },
			{ -0.6646f, -0.7389f, 0.1105f, 0.0041f }, { 0.9256f, 0.3786f, 0.0000f, 0.0000f }, { 0.8914f, 0.4532f, 0.0000f, 0.0000f },
			{ -0.5579f, -0.8211f, 0.0750f, -0.0950f }, { 0.9286f, 0.3710f, 0.0000f, 0.0000f }, { 0.9295f, 0.3689f, 0.0000f, 0.0000f },
			{ -0.4836f, -0.8605f, 0.0466f, -0.1534f }, { 0.9378f, 0.3472f, 0.0000f, 0.0000f }, { 0.9583f, 0.2857f, 0.0000f, 0.0000f },
			{ -0.4093f, -0.8822f, 0.0808f, -0.2183f }, { 0.9556f, 0.2947f, 0.0000f, 0.0000f }, { 0.9862f, 0.1658f, 0.0000f, 0.0000f } } };

		struct Skeleton
		{
			RE::NiAVObject*                           root{ nullptr };
			std::array<RE::NiAVObject*, kBoneCount>   bones{};
			std::array<NiMatrix3, kBoneCount>         animated{};  // the animation's local rotations
			std::array<NiMatrix3, kBoneCount>         written{};   // what we wrote last
			std::array<NiTransform, kBoneCount>       links{};     // chain parent -> the bone's own parent (XPMSSE puts CME nodes in between)
			bool                                      posed{ false };
			NiPoint3                                  fingerAxis;  // hand local: towards the middle finger
			NiPoint3                                  palmAxis;    // hand local: out of the palm
			float                                     palmLength{ 0.0f };
			float                                     hiddenScale{ 0.0f };  // the upper arm's scale before we showed it again, 0 = never hidden
		};
		Skeleton skeleton;

		std::string BoneName(std::size_t a_bone)
		{
			switch (a_bone) {
			case kUpper:
				return "NPC R UpperArm [RUar]";
			case kFore:
				return "NPC R Forearm [RLar]";
			case kHand:
				return "NPC R Hand [RHnd]";
			case kTwist1:
				return "NPC R ForearmTwist1 [RLt1]";
			case kTwist2:
				return "NPC R ForearmTwist2 [RLt2]";
			default:
				{
					const auto index = a_bone - kFirstFinger;
					const auto finger = index / kJoints;
					const auto joint = index % kJoints;
					return std::format("NPC R Finger{}{} [RF{}{}]", finger, joint, finger, joint);
				}
			}
		}

		// multiplies the nodes between a bone and its chain parent (identity on the vanilla skeleton)
		bool UpdateLink(std::size_t a_bone)
		{
			NiTransform link;
			const auto  chainParent = ChainParent(a_bone);
			if (chainParent != kBoneCount) {
				const RE::NiAVObject* target = skeleton.bones[chainParent];
				const RE::NiAVObject* node = skeleton.bones[a_bone]->parent;
				for (int depth = 0; node != target; ++depth) {
					if (!node || depth == 8) {
						return false;
					}
					link = Combine(node->local, link);
					node = node->parent;
				}
			}
			skeleton.links[a_bone] = link;
			return true;
		}

		bool UpdateLinks()
		{
			for (std::size_t i = 0; i < kBoneCount; ++i) {
				if (!UpdateLink(i)) {
					return false;
				}
			}
			return true;
		}

		// empty when the arm was found, else why not
		std::string Search(RE::NiAVObject* a_root)
		{
			skeleton = {};
			for (std::size_t i = 0; i < kBoneCount; ++i) {
				skeleton.bones[i] = a_root->GetObjectByName(BoneName(i).c_str());
				if (!skeleton.bones[i]) {
					skeleton = {};
					return std::format("1st person skeleton has no bone {}", BoneName(i));
				}
			}
			const auto& b = skeleton.bones;
			if (!b[kUpper]->parent) {
				skeleton = {};
				return "1st person upper arm has no parent";
			}
			for (std::size_t i = 0; i < kBoneCount; ++i) {
				if (!UpdateLink(i)) {
					const auto error = std::format("1st person bone {} doesn't hang from {}", BoneName(i), BoneName(ChainParent(i)));
					skeleton = {};
					return error;
				}
			}
			// hand axes from the finger bones' fixed offsets (hand local space)
			const NiPoint3 middle = Combine(skeleton.links[FingerBone(2, 0)], b[FingerBone(2, 0)]->local).translate;
			const NiPoint3 thumb = Combine(skeleton.links[FingerBone(0, 0)], b[FingerBone(0, 0)]->local).translate;
			skeleton.fingerAxis = Normalized(middle);
			skeleton.palmAxis = Normalized(thumb.Cross(middle));  // right hand: thumb x fingers points out of the palm
			skeleton.palmLength = middle.Length();
			skeleton.root = a_root;
			for (std::size_t i = 0; i < kBoneCount; ++i) {
				skeleton.animated[i] = b[i]->local.rotate;
				skeleton.written[i] = b[i]->local.rotate;
			}
			logs::info("Found the 1st person arm (upper arm {:.1f}, forearm {:.1f}, palm {:.1f}{})",
				Combine(skeleton.links[kFore], b[kFore]->local).translate.Length(),
				Combine(skeleton.links[kHand], b[kHand]->local).translate.Length(), skeleton.palmLength,
				b[kFore]->parent != b[kUpper] ? ", extra nodes between the bones" : "");
			return {};
		}

		RE::NiAVObject* warnedRoot{ nullptr };  // the skeleton we already complained about

		// Improved Camera SE shows the 3rd person body in 1st person and hides the 1st person arms by scaling the upper
		// arms to 0.001 every frame (unless a weapon is drawn): the arm and the item placed from the hand would vanish.
		// Shows the right arm again; the hider writes its scale again next frame, so this runs every time.
		void ShowArm()
		{
			const auto upper = skeleton.bones[kUpper];
			if (upper->local.scale >= kHiddenScale) {
				return;
			}
			if (skeleton.hiddenScale == 0.0f) {
				logs::info("The 1st person arm was hidden by another mod (scale {:.3f}), showing it while inspecting", upper->local.scale);
			}
			skeleton.hiddenScale = std::max(upper->local.scale, 1e-4f);
			upper->local.scale = 1.0f;
			RE::NiUpdateData update{};
			upper->Update(update);
		}

		bool Find(RE::NiAVObject* a_root)
		{
			if (skeleton.root == a_root && skeleton.bones[kUpper] && UpdateLinks()) {
				ShowArm();
				return true;
			}
			const auto error = Search(a_root);
			if (error.empty()) {
				warnedRoot = nullptr;
				ShowArm();
				return true;
			}
			if (warnedRoot != a_root) {
				logs::warn("{}, can't pose the arm", error);
				warnedRoot = a_root;
			}
			return false;
		}

		// picks up rotations the animation wrote since our last write
		void RefreshAnimated()
		{
			for (std::size_t i = 0; i < kBoneCount; ++i) {
				const auto& current = skeleton.bones[i]->local.rotate;
				if (!skeleton.posed || !SameRotation(current, skeleton.written[i])) {
					skeleton.animated[i] = current;
				}
			}
		}

		// the bone relative to its chain parent, with the given local rotation
		NiTransform WithRotation(std::size_t a_bone, const NiMatrix3& a_rotation)
		{
			NiTransform local = skeleton.bones[a_bone]->local;
			local.rotate = a_rotation;
			return Combine(skeleton.links[a_bone], local);
		}

		NiTransform AnimatedLocal(std::size_t a_bone)
		{
			return WithRotation(a_bone, skeleton.animated[a_bone]);
		}

		// a world rotation as the bone's local rotation, below the chain parent's world transform
		NiMatrix3 LocalRotation(std::size_t a_bone, const NiMatrix3& a_parentWorld, const NiMatrix3& a_world)
		{
			return (a_parentWorld * skeleton.links[a_bone].rotate).Transpose() * a_world;
		}
	}

	std::optional<Hand> Apply(RE::NiAVObject* a_root, const Goal& a_goal)
	{
		if (!a_root || !Find(a_root)) {
			return std::nullopt;
		}
		RefreshAnimated();
		const auto& b = skeleton.bones;
		const float weight = Clamp01(a_goal.weight);

		// the animated arm in world space
		const NiTransform shoulderParent = b[kUpper]->parent->world;
		const NiTransform upper = Combine(shoulderParent, AnimatedLocal(kUpper));
		const NiTransform fore = Combine(upper, AnimatedLocal(kFore));
		const NiTransform hand = Combine(fore, AnimatedLocal(kHand));

		// two-bone IK
		const NiPoint3 shoulder = upper.translate;
		const float    upperLength = (fore.translate - shoulder).Length();
		const float    foreLength = (hand.translate - fore.translate).Length();
		NiPoint3       toTarget = a_goal.wrist - shoulder;
		const float    reach = std::clamp(toTarget.Length(), std::abs(upperLength - foreLength) + 0.01f, (upperLength + foreLength) * 0.999f);
		const NiPoint3 direction = Normalized(toTarget, Normalized(hand.translate - shoulder));
		const NiPoint3 wrist = shoulder + direction * reach;

		const float along = (upperLength * upperLength - foreLength * foreLength + reach * reach) / (2.0f * reach);
		const float out = std::sqrt(std::max(0.0f, upperLength * upperLength - along * along));
		NiPoint3    pole = a_goal.pole - direction * direction.Dot(a_goal.pole);
		if (pole.Length() < 1e-3f) {
			pole = (fore.translate - shoulder) - direction * direction.Dot(fore.translate - shoulder);
		}
		const NiPoint3 elbow = shoulder + direction * along + Normalized(pole) * out;

		NiTransform ikUpper = upper;
		ikUpper.rotate = RotationBetween(fore.translate - shoulder, elbow - shoulder) * upper.rotate;
		NiTransform     ikFore = Combine(ikUpper, AnimatedLocal(kFore));
		const NiPoint3  handNow = Combine(ikFore, AnimatedLocal(kHand)).translate;
		ikFore.rotate = RotationBetween(handNow - ikFore.translate, wrist - ikFore.translate) * ikFore.rotate;
		const NiMatrix3 ikHandRotation = a_goal.hand ? *a_goal.hand : AlignAxes(skeleton.fingerAxis, skeleton.palmAxis, a_goal.fingers, a_goal.palm);

		// back to local rotations, blended over the animation
		std::array<NiMatrix3, kBoneCount> result = skeleton.animated;
		result[kUpper] = Slerp(skeleton.animated[kUpper], LocalRotation(kUpper, shoulderParent.rotate, ikUpper.rotate), weight);
		result[kFore] = Slerp(skeleton.animated[kFore], LocalRotation(kFore, ikUpper.rotate, ikFore.rotate), weight);
		result[kHand] = Slerp(skeleton.animated[kHand], LocalRotation(kHand, ikFore.rotate, ikHandRotation), weight);

		// spread the wrist's twist over the forearm so the skin doesn't wring at the wrist
		const NiPoint3 forearmAxis = Normalized(b[kHand]->local.translate);  // in the hand's parent space
		const float    twist = TwistAngle(result[kHand] * skeleton.animated[kHand].Transpose(), forearmAxis);
		for (const auto [bone, share] : { std::pair{ kTwist1, 0.66f }, std::pair{ kTwist2, 0.33f } }) {
			const NiPoint3 axis = skeleton.links[bone].rotate.Transpose() * (skeleton.links[kHand].rotate * forearmAxis);
			result[bone] = AxisAngle(axis, twist * share) * skeleton.animated[bone];
		}

		// finger curl around the axis across the palm
		const NiTransform finalUpper = Combine(shoulderParent, WithRotation(kUpper, result[kUpper]));
		const NiTransform finalFore = Combine(finalUpper, WithRotation(kFore, result[kFore]));
		const NiTransform finalHand = Combine(finalFore, WithRotation(kHand, result[kHand]));
		const NiPoint3    fingersWorld = finalHand.rotate * skeleton.fingerAxis;
		const NiPoint3    palmWorld = finalHand.rotate * skeleton.palmAxis;
		const NiPoint3    curlAxis = Normalized(fingersWorld.Cross(palmWorld));
		if (a_goal.weaponGrip) {
			// the game's own weapon grip, blended in with the arm
			for (std::size_t i = 0; i < kWeaponGrip.size(); ++i) {
				const auto bone = kFirstFinger + i;
				result[bone] = Slerp(skeleton.animated[bone], ToMatrix(kWeaponGrip[i]), weight);
			}
		} else {
			for (int finger = 0; finger < kFingers; ++finger) {
				const float curl = a_goal.curl[finger] * (finger == 0 ? kThumbCurl : 1.0f);
				if (curl == 0.0f) {
					continue;
				}
				NiTransform parent = finalHand;
				for (int joint = 0; joint < kJoints; ++joint) {
					const auto     bone = FingerBone(finger, joint);
					const NiPoint3 axis = (parent.rotate * skeleton.links[bone].rotate).Transpose() * curlAxis;
					result[bone] = AxisAngle(Normalized(axis), curl) * skeleton.animated[bone];
					parent = Combine(parent, WithRotation(bone, result[bone]));
				}
			}
		}

		for (std::size_t i = 0; i < kBoneCount; ++i) {
			b[i]->local.rotate = result[i];
			skeleton.written[i] = result[i];
		}
		skeleton.posed = true;
		RE::NiUpdateData update{};
		b[kUpper]->Update(update);

		Hand handResult;
		handResult.world = b[kHand]->world;
		handResult.palmNormal = Normalized(handResult.world.rotate * skeleton.palmAxis);
		handResult.palmCenter = handResult.world.translate +
		                        handResult.world.rotate * (skeleton.fingerAxis * (skeleton.palmLength * 0.55f)) * handResult.world.scale;
		return handResult;
	}

	std::optional<HandFrame> Frame(RE::NiAVObject* a_root)
	{
		if (!a_root || !Find(a_root)) {
			return std::nullopt;
		}
		HandFrame frame{ .fingerAxis = skeleton.fingerAxis, .palmAxis = skeleton.palmAxis, .palmLength = skeleton.palmLength };
		// held weapons sit on the WEAPON node; it's normally a child of the hand, measured through world space to be sure
		if (const auto weapon = a_root->GetObjectByName("WEAPON")) {
			frame.weaponGrip = ToLocal(skeleton.bones[kHand]->world, weapon->world);
		}
		return frame;
	}

	void Release()
	{
		if (skeleton.posed && skeleton.bones[kUpper]) {
			for (std::size_t i = 0; i < kBoneCount; ++i) {
				auto& rotation = skeleton.bones[i]->local.rotate;
				if (SameRotation(rotation, skeleton.written[i])) {
					rotation = skeleton.animated[i];
				}
			}
			// hidden again like we found it (the other mod keeps doing that anyway)
			if (skeleton.hiddenScale > 0.0f && skeleton.bones[kUpper]->local.scale == 1.0f) {
				skeleton.bones[kUpper]->local.scale = skeleton.hiddenScale;
			}
			RE::NiUpdateData update{};
			skeleton.bones[kUpper]->Update(update);
		}
		// the 1st person skeleton can be rebuilt between pickups (game load, race change): look the bones up again
		// next time instead of trusting old pointers
		skeleton = {};
	}

	void Forget()
	{
		skeleton = {};
	}

	bool ShownAgain()
	{
		return skeleton.hiddenScale > 0.0f;
	}
}
