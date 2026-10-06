#include "SmoothCam.h"

#include <Windows.h>

#define SMOOTHCAM_API_COMMONLIB
#include "SmoothCamAPI.h"

namespace SmoothCam
{
	namespace
	{
		SmoothCamAPI::IVSmoothCam3* api{ nullptr };
		bool                        haveCamera{ false };
		bool                        haveCrosshair{ false };
		bool                        warned{ false };  // about a refused camera, once until we give it back

		const char* Name(SmoothCamAPI::APIResult a_result)
		{
			switch (a_result) {
			case SmoothCamAPI::APIResult::OK:
				return "OK";
			case SmoothCamAPI::APIResult::NotOwner:
				return "not owner";
			case SmoothCamAPI::APIResult::MustKeep:
				return "SmoothCam must keep it";
			case SmoothCamAPI::APIResult::AlreadyGiven:
				return "already ours";
			case SmoothCamAPI::APIResult::AlreadyTaken:
				return "another mod has it";
			case SmoothCamAPI::APIResult::BadThread:
				return "wrong thread";
			default:
				return "unknown";
			}
		}
	}

	void Listen()
	{
		if (!GetModuleHandleW(L"SmoothCam.dll")) {
			return;  // not installed: nothing to ask for (and no SKSE error about a missing listener)
		}
		const bool listening = SmoothCamAPI::RegisterInterfaceLoaderCallback(SKSE::GetMessagingInterface(),
			[](void* a_interface, SmoothCamAPI::InterfaceVersion a_version) {
				if (a_version == SmoothCamAPI::InterfaceVersion::V3) {
					api = static_cast<SmoothCamAPI::IVSmoothCam3*>(a_interface);
					logs::info("SmoothCam found: it hands us the camera while we hold an item in 3rd person");
				}
			});
		if (!listening) {
			logs::warn("SmoothCam is loaded, but listening to its API failed: it may keep the camera in 3rd person");
		}
	}

	void Request()
	{
		if (!GetModuleHandleW(L"SmoothCam.dll")) {
			return;
		}
		[[maybe_unused]] const bool listening = SmoothCamAPI::RequestInterface(SKSE::GetMessagingInterface(), SmoothCamAPI::InterfaceVersion::V3);
	}

	void TakeCamera()
	{
		if (!api) {
			return;
		}
		const auto handle = SKSE::GetPluginHandle();
		if (!haveCamera) {
			const auto result = api->RequestCameraControl(handle);
			haveCamera = result == SmoothCamAPI::APIResult::OK || result == SmoothCamAPI::APIResult::AlreadyGiven;
			if (!haveCamera && !warned) {
				warned = true;
				logs::warn("SmoothCam kept the camera ({}; owner {}, we are {}): the camera stays where SmoothCam has it", Name(result),
					api->GetCameraOwner(), handle);
			}
		}
		if (!haveCrosshair) {
			const auto result = api->RequestCrosshairControl(handle, false);
			haveCrosshair = result == SmoothCamAPI::APIResult::OK || result == SmoothCamAPI::APIResult::AlreadyGiven;
		}
	}

	void GiveBack()
	{
		if (!api) {
			return;
		}
		const auto handle = SKSE::GetPluginHandle();
		warned = false;
		if (haveCamera) {
			// it goes on from where it left the camera: the player didn't move meanwhile and our camera ends there too
			[[maybe_unused]] const auto result = api->ReleaseCameraControl(handle);
			haveCamera = false;
		}
		if (haveCrosshair) {
			[[maybe_unused]] const auto result = api->ReleaseCrosshairControl(handle);
			haveCrosshair = false;
		}
	}

	bool HasCamera()
	{
		return !api || haveCamera;
	}
}
