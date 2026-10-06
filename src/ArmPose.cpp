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
		constexpr float kPi = 3.14159265f;
		constexpr float kMaxWristTwist = 2.8f;   // radians (160 degrees) the hinged arm's wrist may turn from its animation
		constexpr float kMaxWristBend = 0.6f;    // radians (35 degrees) the hinged arm's wrist may bend away from its animation
		// The elbow's hinge in upper arm space: the forearm of every skeleton (vanilla, beast, XPMSSE, 1st person) bends
		// around the upper arm's X axis; bending further turns it around -X
		const NiPoint3 kHinge{ -1.0f, 0.0f, 0.0f };
		// A hand without finger bones (the player's 3rd person skeleton can have none until gauntlets bring them): its
		// axes from the reference skeletons' finger offsets (hand space), fingers along +Z, the palm towards -Y
		const NiPoint3  kDefaultFingerAxis{ 0.045f, -0.010f, 0.999f };
		const NiPoint3  kDefaultPalmAxis{ -0.31f, -0.95f, 0.0f };
		constexpr float kDefaultPalmLength = 8.3f;

		constexpr bool Optional(std::size_t a_bone) { return a_bone == kTwist1 || a_bone == kTwist2 || a_bone >= kFirstFinger; }

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
			std::array<std::optional<NiMatrix3>, kBoneCount> open{};  // finger bones: local rotation of the mesh's open hand
			// Skin bones without a node (the player's 3rd person skeleton can lack the twist / finger bones: the skin then
			// keeps bones[i] null and reads its world transform from elsewhere, so posing the arm left that skin behind,
			// torn): the skin is pointed at our own transform, which moves with the posed forearm / hand
			struct Redirect
			{
				RE::NiPointer<RE::NiSkinInstance> skin;
				std::uint32_t                     index;
				const NiTransform*                original;  // what the skin read before
				std::size_t                       follow;    // kFore / kHand
				float                             share;     // of the wrist twist (forearm slots)
				int                               finger{ -1 };  // a finger joint (thumb first), -1 = moves with the hand as it is
				int                               joint{ 0 };
				std::optional<NiMatrix3>          open;          // finger joints: rotation of the mesh's open hand, below the joint before
				NiTransform                       animated;  // this frame, read from original
				NiTransform                       world;     // what the skin reads now
			};
			std::vector<std::unique_ptr<Redirect>> redirects;  // stable addresses: the skins point into them
		};
		Skeleton skeleton;

		// the skins read their own transforms again
		void DropRedirects()
		{
			for (const auto& redirect : skeleton.redirects) {
				auto& pointer = redirect->skin->boneWorldTransforms[redirect->index];
				if (pointer == &redirect->world) {
					pointer = redirect->original;
				}
			}
			skeleton.redirects.clear();
		}

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
			if (!skeleton.bones[a_bone]) {
				skeleton.links[a_bone] = link;
				return true;  // an optional bone the skeleton doesn't have
			}
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

		// all nodes below a_root with this name
		std::vector<RE::NiAVObject*> NodesNamed(RE::NiAVObject* a_root, const std::string& a_name)
		{
			std::vector<RE::NiAVObject*> nodes;
			RE::BSVisit::TraverseScenegraphObjects(a_root, [&](RE::NiAVObject* a_object) {
				if (a_object->name == a_name.c_str()) {
					nodes.push_back(a_object);
				}
				return RE::BSVisit::BSVisitControl::kContinue;
			});
			return nodes;
		}

		// how many skinned meshes below a_root use a_node as a bone
		int SkinUses(RE::NiAVObject* a_root, const RE::NiAVObject* a_node)
		{
			int uses = 0;
			RE::BSVisit::TraverseScenegraphGeometries(a_root, [&](RE::BSGeometry* a_geometry) {
				const auto skin = a_geometry->GetGeometryRuntimeData().skinInstance.get();
				const auto data = skin ? skin->skinData.get() : nullptr;
				if (data && skin->bones) {
					for (std::uint32_t i = 0; i < data->GetBoneCount(); ++i) {
						if (skin->bones[i] == a_node) {
							++uses;
							break;
						}
					}
				}
				return RE::BSVisit::BSVisitControl::kContinue;
			});
			return uses;
		}

		std::string ParentNames(const RE::NiAVObject* a_node, int a_count)
		{
			std::string names;
			for (auto node = a_node->parent; node && a_count > 0; node = node->parent, --a_count) {
				names += std::format(" < {}", node->name.c_str() ? node->name.c_str() : "?");
			}
			return names;
		}

		// the nearest parent of a_node with this name
		RE::NiAVObject* Ancestor(RE::NiAVObject* a_node, const std::string& a_name)
		{
			auto node = a_node ? a_node->parent : nullptr;
			for (int depth = 0; node && depth < 8; node = node->parent, ++depth) {
				if (node->name == a_name.c_str()) {
					return node;
				}
			}
			return nullptr;
		}

		// The right hand the meshes are skinned to. A model can bring its own copy of the arm bones (with the same names,
		// found first by name); posing such a copy left the skin on the real bones behind and tore it. The copy most
		// skinned meshes use wins.
		RE::NiAVObject* SkinnedHand(RE::NiAVObject* a_root)
		{
			const auto      hands = NodesNamed(a_root, BoneName(kHand));
			RE::NiAVObject* best = nullptr;
			int             bestUses = -1;
			for (const auto hand : hands) {
				const int uses = SkinUses(a_root, hand);
				if (hands.size() > 1) {
					logs::info("  right hand bone used by {} skinned meshes:{}", uses, ParentNames(hand, 6));
				}
				if (uses > bestUses) {
					best = hand;
					bestUses = uses;
				}
			}
			if (hands.size() > 1) {
				logs::info("{} right hand bones in the skeleton, posing the one {} meshes use", hands.size(), bestUses);
			}
			return best;
		}

		float DistanceToSegment(const NiPoint3& a_point, const NiPoint3& a_from, const NiPoint3& a_to, float* a_along = nullptr)
		{
			const NiPoint3 segment = a_to - a_from;
			const float    length2 = segment.Dot(segment);
			const float    along = length2 > 1e-6f ? (a_point - a_from).Dot(segment) / length2 : 0.0f;
			if (a_along) {
				*a_along = along;
			}
			return (a_point - (a_from + segment * std::clamp(along, 0.0f, 1.0f))).Length();
		}

		// A mesh's node-less hand slots are its fingers when there are 15 of them that line up as 5 chains of 3 joints,
		// each further from the wrist than the one before (skins list the bones like the skeleton: thumb first)
		bool FingerSlots(const std::vector<Skeleton::Redirect*>& a_slots, const NiPoint3& a_wrist)
		{
			if (a_slots.size() != kFingers * kJoints) {
				return false;
			}
			for (int finger = 0; finger < kFingers; ++finger) {
				float reach = 0.0f;
				for (int joint = 0; joint < kJoints; ++joint) {
					const auto&    slot = a_slots[finger * kJoints + joint];
					const NiPoint3 p = slot->animated.translate;
					const float    distance = (p - a_wrist).Length();
					if (distance <= reach) {
						return false;
					}
					if (joint > 0) {
						const float gap = (p - a_slots[finger * kJoints + joint - 1]->animated.translate).Length();
						if (gap < 0.5f || gap > 6.0f) {
							return false;
						}
					}
					reach = distance;
				}
			}
			for (std::size_t i = 0; i < a_slots.size(); ++i) {
				a_slots[i]->finger = static_cast<int>(i) / kJoints;
				a_slots[i]->joint = static_cast<int>(i) % kJoints;
			}
			return true;
		}

		// A bone's rotation in the mesh's bind pose (the open hand meshes are made in), relative to its parent bone:
		// skinToBone maps the mesh into the bone, so the bone in the mesh is its inverse
		NiMatrix3 BindRotation(const RE::NiSkinData* a_data, std::uint32_t a_parent, std::uint32_t a_bone)
		{
			return a_data->GetBoneDataSkinToBone(a_parent).rotate * a_data->GetBoneDataSkinToBone(a_bone).rotate.Transpose();
		}

		// the open hand for a mesh's finger slots (needs the hand bone in the same mesh)
		void SetOpenSlots(RE::NiSkinInstance* a_skin, const std::vector<Skeleton::Redirect*>& a_slots)
		{
			const auto data = a_skin->skinData.get();
			for (std::uint32_t i = 0; i < data->GetBoneCount(); ++i) {
				if (a_skin->bones[i] != skeleton.bones[kHand]) {
					continue;
				}
				for (const auto slot : a_slots) {
					const auto parent = slot->joint == 0 ? i : a_slots[slot->finger * kJoints + slot->joint - 1]->index;
					slot->open = BindRotation(data, parent, slot->index);
				}
				return;
			}
		}

		// the open hand for real finger bones, from a mesh skinned to the finger and the bone before it
		void FindOpenFingers(RE::NiAVObject* a_root)
		{
			RE::BSVisit::TraverseScenegraphGeometries(a_root, [&](RE::BSGeometry* a_geometry) {
				const auto skin = a_geometry->GetGeometryRuntimeData().skinInstance.get();
				const auto data = skin ? skin->skinData.get() : nullptr;
				if (!data || !skin->bones) {
					return RE::BSVisit::BSVisitControl::kContinue;
				}
				std::unordered_map<const RE::NiAVObject*, std::uint32_t> indices;
				for (std::uint32_t i = 0; i < data->GetBoneCount(); ++i) {
					if (skin->bones[i]) {
						indices.emplace(skin->bones[i], i);
					}
				}
				for (std::size_t bone = kFirstFinger; bone < kBoneCount; ++bone) {
					const auto own = indices.find(skeleton.bones[bone]);
					const auto parent = indices.find(skeleton.bones[ChainParent(bone)]);
					if (!skeleton.open[bone] && skeleton.bones[bone] && own != indices.end() && parent != indices.end()) {
						// relative to the chain parent; the bone's own local is below the nodes in between
						skeleton.open[bone] = skeleton.links[bone].rotate.Transpose() * BindRotation(data, parent->second, own->second);
					}
				}
				return RE::BSVisit::BSVisitControl::kContinue;
			});
		}

		// The node-less bones of the meshes skinned to the arm, by where they are now (the animated pose): near the
		// forearm -> follow the forearm (twist bones), near the hand -> follow the hand (fingers), else left alone
		void FindRedirects(RE::NiAVObject* a_root)
		{
			const auto&     b = skeleton.bones;
			const NiPoint3  elbow = b[kFore]->world.translate;
			const NiPoint3  wrist = b[kHand]->world.translate;
			const NiPoint3  tips = wrist + b[kHand]->world.rotate * skeleton.fingerAxis * (skeleton.palmLength * 1.6f * b[kHand]->world.scale);
			constexpr float kNear = 8.0f;
			int             distant = 0;
			int             fingerMeshes = 0;
			RE::BSVisit::TraverseScenegraphGeometries(a_root, [&](RE::BSGeometry* a_geometry) {
				const auto skin = a_geometry->GetGeometryRuntimeData().skinInstance.get();
				const auto data = skin ? skin->skinData.get() : nullptr;
				if (!data || !skin->bones || !skin->boneWorldTransforms) {
					return RE::BSVisit::BSVisitControl::kContinue;
				}
				const auto count = data->GetBoneCount();
				bool       uses = false;
				for (std::uint32_t i = 0; i < count; ++i) {
					uses |= skin->bones[i] && std::ranges::find(b, skin->bones[i]) != b.end();
				}
				if (!uses) {
					return RE::BSVisit::BSVisitControl::kContinue;
				}
				std::vector<Skeleton::Redirect*> handSlots;
				for (std::uint32_t i = 0; i < count; ++i) {
					const auto world = skin->boneWorldTransforms[i];
					if (skin->bones[i] || !world) {
						continue;
					}
					const NiPoint3 p = world->translate;
					float          along = 0.0f;
					const float    toFore = DistanceToSegment(p, elbow, wrist, &along);
					const float    toHand = DistanceToSegment(p, wrist, tips);
					if (std::min(toFore, toHand) > kNear) {
						++distant;  // the other arm
					} else {
						const bool hand = toHand < toFore;
						skeleton.redirects.push_back(std::make_unique<Skeleton::Redirect>(Skeleton::Redirect{ RE::NiPointer<RE::NiSkinInstance>(skin), i,
							world, hand ? kHand : kFore, hand ? 0.0f : std::clamp(along, 0.0f, 1.0f) }));
						skeleton.redirects.back()->animated = *world;
						skeleton.redirects.back()->world = *world;
						if (hand) {
							handSlots.push_back(skeleton.redirects.back().get());
						}
					}
				}
				if (FingerSlots(handSlots, wrist)) {
					SetOpenSlots(skin, handSlots);
					++fingerMeshes;
				}
				return RE::BSVisit::BSVisitControl::kContinue;
			});
			if (!skeleton.redirects.empty()) {
				logs::info("{} skin bones without a node follow the arm ({} meshes with fingers), {} of the other arm left alone",
					skeleton.redirects.size(), fingerMeshes, distant);
			}
		}

		// ---- diagnostics: the right arm's nodes as the game has them ----

		std::unordered_map<const RE::NiAVObject*, int> AllSkinUses(RE::NiAVObject* a_root)
		{
			std::unordered_map<const RE::NiAVObject*, int> uses;
			RE::BSVisit::TraverseScenegraphGeometries(a_root, [&](RE::BSGeometry* a_geometry) {
				const auto skin = a_geometry->GetGeometryRuntimeData().skinInstance.get();
				const auto data = skin ? skin->skinData.get() : nullptr;
				if (data && skin->bones) {
					for (std::uint32_t i = 0; i < data->GetBoneCount(); ++i) {
						if (skin->bones[i]) {
							++uses[skin->bones[i]];
						}
					}
				}
				return RE::BSVisit::BSVisitControl::kContinue;
			});
			return uses;
		}

		void DumpNode(const RE::NiAVObject* a_node, int a_depth, const std::unordered_map<const RE::NiAVObject*, int>& a_uses)
		{
			const auto&  r = a_node->local.rotate;
			const bool   identity = SameRotation(r, NiMatrix3{});
			const auto   use = a_uses.find(a_node);
			const char*  name = a_node->name.c_str() ? a_node->name.c_str() : "?";
			logs::info("  {:{}}{} t=({:.2f}, {:.2f}, {:.2f}){}{}", "", a_depth * 2, name, a_node->local.translate.x, a_node->local.translate.y,
				a_node->local.translate.z, identity ? "" : " rotated", use != a_uses.end() ? std::format(", skin bone of {} meshes", use->second) : "");
			const auto node = a_depth < 7 ? const_cast<RE::NiAVObject*>(a_node)->AsNode() : nullptr;
			if (!node) {
				return;
			}
			for (const auto& child : node->GetChildren()) {
				if (child && child->AsNode()) {
					DumpNode(child.get(), a_depth + 1, a_uses);
				}
			}
		}

		void DumpArm(RE::NiAVObject* a_root)
		{
			const auto uses = AllSkinUses(a_root);
			for (const auto clavicle : NodesNamed(a_root, "NPC R Clavicle [RClv]")) {
				logs::info("Right arm nodes below {}{}:", clavicle->name.c_str(), ParentNames(clavicle, 3));
				DumpNode(clavicle, 0, uses);
			}
		}

		// empty when the arm was found, else why not
		std::string Search(RE::NiAVObject* a_root)
		{
			DropRedirects();
			skeleton = {};
			// the chain from the skinned hand: the arm bones above it, the twist bones below the forearm, the fingers
			// below the hand
			auto& b = skeleton.bones;
			b[kHand] = SkinnedHand(a_root);
			b[kFore] = Ancestor(b[kHand], BoneName(kFore));
			b[kUpper] = Ancestor(b[kFore], BoneName(kUpper));
			for (const auto bone : { kUpper, kFore, kHand }) {
				if (!b[bone]) {
					skeleton = {};
					return std::format("the skeleton has no bone {} above the right hand", BoneName(bone));
				}
			}
			if (!b[kUpper]->parent) {
				skeleton = {};
				return "the upper arm has no parent";
			}
			// twist bones and fingers if the skeleton has them where they belong
			int missing = 0;
			for (std::size_t i = 0; i < kBoneCount; ++i) {
				if (Optional(i)) {
					const auto parent = (i == kTwist1 || i == kTwist2) ? b[kUpper] : b[kHand];
					b[i] = parent->GetObjectByName(BoneName(i).c_str());
				}
				if (!UpdateLink(i)) {
					if (!Optional(i)) {
						const auto error = std::format("bone {} doesn't hang from {}", BoneName(i), BoneName(ChainParent(i)));
						skeleton = {};
						return error;
					}
					b[i] = nullptr;
					UpdateLink(i);
				}
				missing += b[i] ? 0 : 1;
			}
			// hand axes from the finger bones' fixed offsets (hand local space)
			const auto middleBone = b[FingerBone(2, 0)];
			const auto thumbBone = b[FingerBone(0, 0)];
			if (middleBone && thumbBone) {
				const NiPoint3 middle = Combine(skeleton.links[FingerBone(2, 0)], middleBone->local).translate;
				const NiPoint3 thumb = Combine(skeleton.links[FingerBone(0, 0)], thumbBone->local).translate;
				skeleton.fingerAxis = Normalized(middle);
				skeleton.palmAxis = Normalized(thumb.Cross(middle));  // right hand: thumb x fingers points out of the palm
				skeleton.palmLength = middle.Length();
			} else {
				skeleton.fingerAxis = kDefaultFingerAxis;
				skeleton.palmAxis = kDefaultPalmAxis;
				skeleton.palmLength = kDefaultPalmLength;
			}
			skeleton.root = a_root;
			for (std::size_t i = 0; i < kBoneCount; ++i) {
				if (b[i]) {
					skeleton.animated[i] = b[i]->local.rotate;
					skeleton.written[i] = b[i]->local.rotate;
				}
			}
			logs::info("Found the arm (upper arm {:.1f}, forearm {:.1f}, palm {:.1f}{}{}), hanging from{}",
				Combine(skeleton.links[kFore], b[kFore]->local).translate.Length(),
				Combine(skeleton.links[kHand], b[kHand]->local).translate.Length(), skeleton.palmLength,
				b[kFore]->parent != b[kUpper] ? ", extra nodes between the bones" : "",
				missing ? std::format(", {} of its twist / finger bones missing", missing) : "", ParentNames(b[kUpper], 5));
			FindRedirects(a_root);
			FindOpenFingers(a_root);
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
				DumpArm(a_root);
				warnedRoot = a_root;
			}
			return false;
		}

		// picks up rotations the animation wrote since our last write
		void RefreshAnimated()
		{
			for (std::size_t i = 0; i < kBoneCount; ++i) {
				if (!skeleton.bones[i]) {
					continue;
				}
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
		std::erase_if(skeleton.redirects, [](const auto& a_redirect) {
			const auto pointer = a_redirect->skin->boneWorldTransforms[a_redirect->index];
			if (pointer != a_redirect->original && pointer != &a_redirect->world) {
				return true;  // the skin was linked again meanwhile, its new transform is the game's business
			}
			a_redirect->animated = *a_redirect->original;
			return false;
		});
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
		NiTransform ikFore;
		if (a_goal.hinge) {
			// the elbow bends only around its hinge (upper arm X in the skeletons), by the angle that gives the reach
			const NiTransform foreRel = AnimatedLocal(kFore);  // the forearm in upper arm space
			const NiPoint3    handOffset = AnimatedLocal(kHand).translate;
			const NiPoint3    bendNow = Normalized(foreRel.rotate * handOffset);
			const NiPoint3    upperAxis = Normalized(foreRel.translate);
			const float       bentNow = std::atan2(kHinge.Dot(upperAxis.Cross(bendNow)), upperAxis.Dot(bendNow));
			const float       inside = std::acos(std::clamp(
				(upperLength * upperLength + foreLength * foreLength - reach * reach) / (2.0f * upperLength * foreLength), -1.0f, 1.0f));
			NiTransform       foreNew = foreRel;
			foreNew.rotate = AxisAngle(kHinge, (kPi - inside) - bentNow) * foreRel.rotate;
			// then the whole arm turns at the shoulder: the wrist onto the target, the elbow towards the pole
			const NiPoint3 wristRel = Combine(foreNew, AnimatedLocal(kHand)).translate;
			ikUpper.rotate = RotationBetween(upper.rotate * wristRel, direction) * upper.rotate;
			const NiPoint3 elbowDir = ikUpper.rotate * foreNew.translate;
			const NiPoint3 elbowSide = elbowDir - direction * direction.Dot(elbowDir);
			if (elbowSide.Length() > 1e-3f) {
				const float roll = std::atan2(direction.Dot(elbowSide.Cross(pole)), elbowSide.Dot(pole));
				ikUpper.rotate = AxisAngle(direction, roll) * ikUpper.rotate;
			}
			ikFore.rotate = ikUpper.rotate * foreNew.rotate;
		} else {
			ikUpper.rotate = RotationBetween(fore.translate - shoulder, elbow - shoulder) * upper.rotate;
			ikFore = Combine(ikUpper, AnimatedLocal(kFore));
			const NiPoint3 handNow = Combine(ikFore, AnimatedLocal(kHand)).translate;
			ikFore.rotate = RotationBetween(handNow - ikFore.translate, wrist - ikFore.translate) * ikFore.rotate;
		}
		const NiMatrix3 ikHandRotation = a_goal.hand ? *a_goal.hand : AlignAxes(skeleton.fingerAxis, skeleton.palmAxis, a_goal.fingers, a_goal.palm);

		// back to local rotations, blended over the animation
		std::array<NiMatrix3, kBoneCount> result = skeleton.animated;
		result[kUpper] = Slerp(skeleton.animated[kUpper], LocalRotation(kUpper, shoulderParent.rotate, ikUpper.rotate), weight);
		result[kFore] = Slerp(skeleton.animated[kFore], LocalRotation(kFore, ikUpper.rotate, ikFore.rotate), weight);
		result[kHand] = Slerp(skeleton.animated[kHand], LocalRotation(kHand, ikFore.rotate, ikHandRotation), weight);

		// spread the wrist's twist over the forearm so the skin doesn't wring at the wrist
		const NiPoint3 forearmAxis = Normalized(b[kHand]->local.translate);  // in the hand's parent space
		if (a_goal.hinge) {
			// the wrist bends at most so far from its animation (it may still turn around the forearm): the hand points
			// a bit off rather than the wrist folding over
			const NiMatrix3 delta = result[kHand] * skeleton.animated[kHand].Transpose();
			const NiMatrix3 turn = AxisAngle(forearmAxis, TwistAngle(delta, forearmAxis));
			const NiMatrix3 bend = delta * turn.Transpose();
			const float     bent = 2.0f * std::acos(std::clamp(std::abs(ToQuat(bend).w), 0.0f, 1.0f));
			if (bent > kMaxWristBend) {
				result[kHand] = Slerp(NiMatrix3{}, bend, kMaxWristBend / bent) * turn * skeleton.animated[kHand];
			}
		}
		float          twist = TwistAngle(result[kHand] * skeleton.animated[kHand].Transpose(), forearmAxis);
		if (a_goal.hinge && std::abs(twist) > kMaxWristTwist) {
			// more than a wrist turns: the palm stays a bit off rather than the forearm wringing
			const float limited = std::clamp(twist, -kMaxWristTwist, kMaxWristTwist);
			result[kHand] = AxisAngle(forearmAxis, limited - twist) * result[kHand];
			twist = limited;
		}
		for (const auto [bone, share] : { std::pair{ kTwist1, 0.66f }, std::pair{ kTwist2, 0.33f } }) {
			if (!b[bone]) {
				continue;
			}
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
				if (!b[bone]) {
					continue;
				}
				result[bone] = Slerp(skeleton.animated[bone], ToMatrix(kWeaponGrip[i]), weight);
			}
		} else {
			for (std::size_t bone = kFirstFinger; bone < kBoneCount; ++bone) {
				if (a_goal.open > 0.0f && skeleton.open[bone]) {
					result[bone] = Slerp(skeleton.animated[bone], *skeleton.open[bone], a_goal.open * weight);
				}
			}
			for (int finger = 0; finger < kFingers; ++finger) {
				const float curl = a_goal.curl[finger] * (finger == 0 ? kThumbCurl : 1.0f);
				if (curl == 0.0f || !b[FingerBone(finger, 0)] || !b[FingerBone(finger, 1)] || !b[FingerBone(finger, 2)]) {
					continue;
				}
				NiTransform parent = finalHand;
				for (int joint = 0; joint < kJoints; ++joint) {
					const auto     bone = FingerBone(finger, joint);
					const NiPoint3 axis = (parent.rotate * skeleton.links[bone].rotate).Transpose() * curlAxis;
					result[bone] = AxisAngle(Normalized(axis), curl) * result[bone];
					parent = Combine(parent, WithRotation(bone, result[bone]));
				}
			}
		}

		for (std::size_t i = 0; i < kBoneCount; ++i) {
			if (b[i]) {
				b[i]->local.rotate = result[i];
				skeleton.written[i] = result[i];
			}
		}
		skeleton.posed = true;
		RE::NiUpdateData update{};
		b[kUpper]->Update(update);

		// the skin bones without a node: finger slots chained below the hand like finger bones (base joint first, as
		// found), the others moved with the forearm (with their share of the wrist twist) or the hand
		const NiPoint3 twistAxis = skeleton.links[kHand].rotate * forearmAxis;
		// finger slots: each joint below the one before it (base joint first, as found), bent like the finger bones
		NiTransform fingerParentAnimated = hand;
		NiTransform fingerParentPosed = b[kHand]->world;
		for (const auto& redirect : skeleton.redirects) {
			if (redirect->finger >= 0) {
				if (redirect->joint == 0) {
					fingerParentAnimated = hand;
					fingerParentPosed = b[kHand]->world;
				}
				NiTransform local = ToLocal(fingerParentAnimated, redirect->animated);
				if (a_goal.weaponGrip) {
					local.rotate = Slerp(local.rotate, ToMatrix(kWeaponGrip[redirect->finger * kJoints + redirect->joint]), weight);
				} else {
					if (a_goal.open > 0.0f && redirect->open) {
						local.rotate = Slerp(local.rotate, *redirect->open, a_goal.open * weight);
					}
					if (const float curl = a_goal.curl[redirect->finger] * (redirect->finger == 0 ? kThumbCurl : 1.0f); curl != 0.0f) {
						local.rotate = AxisAngle(Normalized(fingerParentPosed.rotate.Transpose() * curlAxis), curl) * local.rotate;
					}
				}
				fingerParentAnimated = redirect->animated;
				redirect->world = Combine(fingerParentPosed, local);
				fingerParentPosed = redirect->world;
				redirect->skin->boneWorldTransforms[redirect->index] = &redirect->world;
				continue;
			}
			const bool  onHand = redirect->follow == kHand;
			NiTransform relative = ToLocal(onHand ? hand : fore, redirect->animated);
			if (!onHand) {
				relative.rotate = AxisAngle(twistAxis, twist * redirect->share) * relative.rotate;
			}
			redirect->world = Combine(onHand ? b[kHand]->world : b[kFore]->world, relative);
			redirect->skin->boneWorldTransforms[redirect->index] = &redirect->world;
		}

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
				if (!skeleton.bones[i]) {
					continue;
				}
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
		DropRedirects();
		// the 1st person skeleton can be rebuilt between pickups (game load, race change): look the bones up again
		// next time instead of trusting old pointers
		skeleton = {};
	}

	void Forget()
	{
		DropRedirects();  // we hold the skins, so they're still there to point back
		skeleton = {};
	}

	bool ShownAgain()
	{
		return skeleton.hiddenScale > 0.0f;
	}
}
