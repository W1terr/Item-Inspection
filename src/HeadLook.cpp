#include "HeadLook.h"

#include "MathUtil.h"

namespace HeadLook
{
	using namespace MathUtil;

	namespace
	{
		constexpr float kMaxYaw = 1.05f;    // radians the face turns away from where the body faces: about 60 degrees each way
		constexpr float kMaxUp = 0.45f;     // ... about 25 degrees up
		constexpr float kMaxDown = 0.9f;    // ... about 50 degrees down (the item is held low in front)
		constexpr float kNeckShare = 0.35f;  // part of the turn the neck takes, the head turns the rest on top
		// cos 35 degrees: a face direction measured this close to the head bone's +Y (where it is in the vanilla and
		// XPMSSE skeletons) is taken as exactly +Y, so an idle's nod at the start doesn't skew it
		constexpr float kFaceAxisTolerance = 0.82f;

		struct Bone
		{
			RE::NiPointer<RE::NiAVObject> node;
			NiMatrix3                     animated;
			NiMatrix3                     written;
			bool                          wrote{ false };

			// back to the animation's rotation of this frame (our own from last frame doesn't count as animated)
			void Restore()
			{
				const NiMatrix3 current = node->local.rotate;
				if (!(wrote && SameRotation(current, written))) {
					animated = current;
				}
				node->local.rotate = animated;
			}

			void Write(const NiMatrix3& a_local)
			{
				node->local.rotate = a_local;
				written = a_local;
				wrote = true;
			}
		};

		const RE::NiAVObject* body{ nullptr };  // the body the bones were found in (only compared)
		Bone                  neck;
		Bone                  head;
		NiPoint3              faceAxis{ 0.0f, 1.0f, 0.0f };  // head bone space: where the face looks
		bool                  faceMeasured{ false };
		bool                  warned{ false };

		void Update(RE::NiAVObject* a_node)
		{
			RE::NiUpdateData update{};
			a_node->Update(update);
		}

		bool Find(RE::NiAVObject* a_root)
		{
			if (body == a_root && neck.node && head.node) {
				return true;
			}
			Forget();
			// the head below the neck: the face's own model can carry nodes of the same name
			const auto neckNode = a_root->GetObjectByName("NPC Neck [Neck]");
			const auto headNode = neckNode ? neckNode->GetObjectByName("NPC Head [Head]") : nullptr;
			if (!headNode || !neckNode->parent || !headNode->parent) {
				if (!warned) {
					logs::warn("No neck / head bones in the player's body: the head doesn't turn to the item");
					warned = true;
				}
				return false;
			}
			body = a_root;
			neck.node.reset(neckNode);
			head.node.reset(headNode);
			return true;
		}

		// yaw (+ = right) and pitch (+ = up) of a direction in the frame the player faces
		std::pair<float, float> Angles(const NiPoint3& a_direction, const NiPoint3& a_forward, const NiPoint3& a_right)
		{
			return { std::atan2(a_direction.Dot(a_right), a_direction.Dot(a_forward)), std::asin(std::clamp(a_direction.z, -1.0f, 1.0f)) };
		}
	}

	void Apply(RE::NiAVObject* a_root, const NiPoint3& a_target, const NiPoint3& a_facing, float a_weight)
	{
		if (a_weight <= 1e-3f) {
			Release();
			return;
		}
		if (!a_root || !Find(a_root)) {
			return;
		}
		neck.Restore();
		head.Restore();
		Update(neck.node.get());  // the head's world from the animated neck

		const NiMatrix3 headWorld = head.node->world.rotate;
		const NiPoint3  up{ 0.0f, 0.0f, 1.0f };
		const NiPoint3  forward = Normalized(NiPoint3{ a_facing.x, a_facing.y, 0.0f }, { 0.0f, 1.0f, 0.0f });
		const NiPoint3  right = forward.Cross(up);
		if (!faceMeasured) {
			const NiPoint3 measured = Normalized(headWorld.Transpose() * forward, { 0.0f, 1.0f, 0.0f });
			faceAxis = measured.y >= kFaceAxisTolerance ? NiPoint3{ 0.0f, 1.0f, 0.0f } : measured;
			faceMeasured = true;
			logs::info("Head looks along ({:.2f}, {:.2f}, {:.2f}) in its bone (measured ({:.2f}, {:.2f}, {:.2f}))", faceAxis.x, faceAxis.y, faceAxis.z,
				measured.x, measured.y, measured.z);
		}
		const NiPoint3 face = Normalized(headWorld * faceAxis, forward);
		const NiPoint3 toTarget = Normalized(a_target - head.node->world.translate, face);

		// within what a neck can do, measured from where the body faces
		const auto [wantYaw, wantPitch] = Angles(toTarget, forward, right);
		const float    yaw = std::clamp(wantYaw, -kMaxYaw, kMaxYaw);
		const float    pitch = std::clamp(wantPitch, -kMaxDown, kMaxUp);
		const NiPoint3 wanted = forward * (std::cos(pitch) * std::cos(yaw)) + right * (std::cos(pitch) * std::sin(yaw)) + up * std::sin(pitch);

		const NiMatrix3 turn = Slerp(NiMatrix3{}, RotationBetween(face, wanted), a_weight);
		const NiMatrix3 neckTurn = Slerp(NiMatrix3{}, turn, kNeckShare);
		neck.Write(neck.node->parent->world.rotate.Transpose() * (neckTurn * neck.node->world.rotate));
		Update(neck.node.get());
		// the head ends up turned by the whole turn: the neck's part plus the rest on top
		head.Write(head.node->parent->world.rotate.Transpose() * (turn * headWorld));
		Update(head.node.get());
	}

	void Release()
	{
		bool changed = false;
		for (const auto bone : { &head, &neck }) {
			if (bone->node && bone->wrote && SameRotation(bone->node->local.rotate, bone->written)) {
				bone->node->local.rotate = bone->animated;
				changed = true;
			}
		}
		if (changed) {
			Update(neck.node.get());
		}
		Forget();
	}

	void Forget()
	{
		neck = {};
		head = {};
		body = nullptr;
		faceMeasured = false;
	}
}
