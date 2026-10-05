#include "Telekinesis.h"

#include "MathUtil.h"

namespace Telekinesis
{
	namespace
	{
		using namespace MathUtil;

		constexpr RE::FormID kTelekinesisSpell = 0x01A4CC;  // Skyrim.esm SPEL "Telekinesis"
		constexpr RE::FormID kGrabSound = 0x053A5D;         // Skyrim.esm SNDR "MAGAlterationTelekinesisGrabSD"
		constexpr float      kSafetyDuration = 120.0f;      // the art ends by itself after this if we never stop it
		constexpr float      kLateWait = 1.0f;              // how long a stopped effect may still show up
		constexpr float      kGrowTime = 0.25f;             // the effect grows in once it sits on the palm

		// from the spell's magic effect
		RE::BGSArtObject*            art{ nullptr };
		RE::TESObjectLIGH*           handLight{ nullptr };
		RE::BGSSoundDescriptorForm*  grabSound{ nullptr };
		bool                         looked{ false };

		// ApplyArtObject doesn't always create the effect right away (its model loads in the background), and on 1.6+
		// it returns a success flag, not the effect. So effects of our art on the player are picked out of the game's
		// list every frame; the ones that were there before we started aren't ours. A new effect first shows up away
		// from the hand for a few frames, so it stays hidden (scale 0) until Place put it on the palm.
		struct Effect
		{
			RE::ModelReferenceEffect* effect{ nullptr };
			bool                      placed{ false };
			float                     age{ 0.0f };  // since it was placed
		};
		std::vector<const RE::ModelReferenceEffect*> foreign;
		std::vector<Effect>                          ours;
		bool                                         running{ false };
		bool                                         stopping{ false };
		float                                        sinceStop{ 0.0f };
		float                                        size{ 1.0f };
		RE::NiPointer<RE::NiLight>                   light;

		void Lookup()
		{
			if (looked) {
				return;
			}
			looked = true;
			const auto data = RE::TESDataHandler::GetSingleton();
			grabSound = data->LookupForm<RE::BGSSoundDescriptorForm>(kGrabSound, "Skyrim.esm");
			const auto spell = data->LookupForm<RE::SpellItem>(kTelekinesisSpell, "Skyrim.esm");
			if (!spell) {
				logs::warn("Telekinesis spell not found, no hand effect");
				return;
			}
			for (const auto effect : spell->effects) {
				const auto base = effect ? effect->baseEffect : nullptr;
				if (!base) {
					continue;
				}
				art = art ? art : base->data.castingArt;
				handLight = handLight ? handLight : base->data.light;
			}
			logs::info("Telekinesis look: art {}, light {}, grab sound {}", art != nullptr, handLight != nullptr, grabSound != nullptr);
		}

		// Built the way the game's magic casters build their sounds (flags 0), following the node
		void PlayOnce(RE::BGSSoundDescriptorForm* a_descriptor, RE::NiAVObject* a_node)
		{
			const auto        audio = RE::BSAudioManager::GetSingleton();
			RE::BSSoundHandle handle;
			if (!audio || !a_descriptor || !audio->GetSoundHandle(handle, a_descriptor, 0)) {
				return;
			}
			if (a_node) {
				handle.SetObjectToFollow(a_node);
			}
			handle.Play();
		}

		template <class F>
		void ForEachOfOurArt(F&& a_func)
		{
			const auto lists = RE::ProcessLists::GetSingleton();
			const auto player = RE::PlayerCharacter::GetSingleton();
			if (!lists || !player || !art) {
				return;
			}
			const auto handle = player->GetHandle().native_handle();
			lists->ForEachModelEffect([&](RE::ModelReferenceEffect* a_effect) {
				if (a_effect && a_effect->artObject == art && a_effect->target.native_handle() == handle) {
					a_func(a_effect);
				}
				return RE::BSContainer::ForEachResult::kContinue;
			});
		}

		void Claim(float a_delta)
		{
			std::vector<Effect> alive;
			ForEachOfOurArt([&](RE::ModelReferenceEffect* a_effect) {
				if (std::ranges::find(foreign, a_effect) != foreign.end()) {
					return;
				}
				const auto known = std::ranges::find(ours, a_effect, &Effect::effect);
				Effect     entry = known != ours.end() ? *known : Effect{ .effect = a_effect };
				if (entry.placed) {
					entry.age += a_delta;
				}
				if (stopping) {
					a_effect->finished = true;
				} else if (!entry.placed && a_effect->artObject3D) {
					a_effect->artObject3D->local.scale = 0.0f;
				}
				alive.push_back(entry);
			});
			ours = std::move(alive);  // forget deleted ones (their addresses get reused)
		}

		void RemoveLight()
		{
			if (!light) {
				return;
			}
			for (const auto scene : RE::BSShaderManager::State::GetSingleton().shadowSceneNode) {
				if (scene) {
					scene->RemoveLight(light.get());
				}
			}
			if (light->parent) {
				light->parent->DetachChild(light.get());
			}
			light.reset();
		}
	}

	void Start(RE::NiAVObject* a_handNode, const Options& a_options)
	{
		Stop();
		Lookup();
		const auto player = RE::PlayerCharacter::GetSingleton();
		if (!player || !a_handNode) {
			return;
		}
		size = std::clamp(a_options.size, 0.1f, 10.0f);
		if (art) {
			foreign.clear();
			ForEachOfOurArt([&](RE::ModelReferenceEffect* a_effect) { foreign.push_back(a_effect); });
			player->ApplyArtObject(art, kSafetyDuration, nullptr, false, false, a_handNode);
			running = true;
			stopping = false;
			Claim(0.0f);
		}
		if (a_options.light && handLight) {
			if (const auto node = a_handNode->AsNode()) {
				light.reset(handLight->GenDynamic(player, node, true, true, false));
			}
		}
	}

	void PlayGrabSound(RE::NiAVObject* a_node)
	{
		Lookup();
		PlayOnce(grabSound, a_node);
	}

	void Stop()
	{
		RemoveLight();
		if (running && !stopping) {
			stopping = true;
			sinceStop = 0.0f;
			Claim(0.0f);
		}
	}

	void Place(const RE::NiTransform& a_world)
	{
		if (!running || stopping) {
			return;
		}
		for (auto& entry : ours) {
			const auto art3D = entry.effect->artObject3D.get();
			if (!art3D || !art3D->parent) {
				continue;
			}
			RE::NiTransform local = ToLocal(art3D->parent->world, a_world);
			local.scale = size * Smooth(std::clamp(entry.age / kGrowTime, 0.0f, 1.0f));  // the vanilla hand effect is tiny
			art3D->local = local;
			RE::NiUpdateData update{};
			art3D->Update(update);
			entry.placed = true;
		}
	}

	void Update(float a_delta)
	{
		if (!running) {
			return;
		}
		Claim(a_delta);
		if (stopping) {
			sinceStop += a_delta;
			if (sinceStop > kLateWait && ours.empty()) {
				running = false;
				stopping = false;
				foreign.clear();
			}
		}
	}

	void Forget()
	{
		foreign.clear();
		ours.clear();
		running = false;
		stopping = false;
		light.reset();  // went away with the old scene
	}
}
