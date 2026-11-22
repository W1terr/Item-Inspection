#include "PoseBlend.h"

#include "MathUtil.h"

namespace PoseBlend
{
	namespace
	{
		using namespace MathUtil;

		struct Bone
		{
			RE::NiPointer<RE::NiAVObject> node;
			NiTransform                   local;
		};

		RE::NiAVObject*   capturedRoot{ nullptr };  // only compared, never used
		std::vector<Bone> bones;

		void Collect(RE::NiAVObject* a_object)
		{
			const auto node = a_object ? a_object->AsNode() : nullptr;
			if (!node) {
				return;
			}
			if (std::string_view(node->name.c_str()).starts_with("NPC ")) {
				bones.push_back({ RE::NiPointer<RE::NiAVObject>(node), node->local });
			}
			for (const auto& child : node->GetChildren()) {
				Collect(child.get());
			}
		}
	}

	void Capture(RE::NiAVObject* a_root)
	{
		Release();
		if (!a_root) {
			return;
		}
		Collect(a_root);
		capturedRoot = a_root;
		logs::info("Holding the pose of {} bones while the body goes over", bones.size());
	}

	bool Apply(RE::NiAVObject* a_root, float a_weight)
	{
		if (!a_root || a_root != capturedRoot || bones.empty()) {
			return false;
		}
		const float w = Clamp01(a_weight);
		for (const auto& bone : bones) {
			auto& local = bone.node->local;
			local.rotate = Slerp(bone.local.rotate, local.rotate, w);
			local.translate = Lerp(bone.local.translate, local.translate, w);
		}
		RE::NiUpdateData update{};
		a_root->Update(update);
		return true;
	}

	void Release()
	{
		bones.clear();
		capturedRoot = nullptr;
	}
}
