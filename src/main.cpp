#include "Inspect.h"
#include "Lang.h"
#include "Menu.h"
#include "Seen.h"
#include "Settings.h"
#include "SmoothCam.h"

namespace
{
	void OnMessage(SKSE::MessagingInterface::Message* a_msg)
	{
		switch (a_msg->type) {
		case SKSE::MessagingInterface::kPostLoad:
			SmoothCam::Listen();
			break;
		case SKSE::MessagingInterface::kPostPostLoad:
			SmoothCam::Request();
			break;
		case SKSE::MessagingInterface::kInputLoaded:
			Inspect::RegisterInput();
			break;
		case SKSE::MessagingInterface::kDataLoaded:
			Settings::Load();
			Lang::Set(Settings::Get().language);
			Menu::Register();
			break;
		case SKSE::MessagingInterface::kPreLoadGame:
		case SKSE::MessagingInterface::kNewGame:
			Inspect::Reset();
			break;
		default:
			break;
		}
	}
}

SKSE_PLUGIN_LOAD(const SKSE::LoadInterface* a_skse)
{
	SKSE::Init(a_skse, { .trampoline = true, .trampolineSize = 128 });  // inventory zoom calls, player skeleton update call
	logs::info("Item Inspection loading");

	Seen::Register();
	Inspect::InstallHooks();
	SKSE::GetMessagingInterface()->RegisterListener(OnMessage);
	return true;
}
