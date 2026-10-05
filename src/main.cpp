#include "Inspect.h"
#include "Lang.h"
#include "Menu.h"
#include "Seen.h"
#include "Settings.h"

namespace
{
	void OnMessage(SKSE::MessagingInterface::Message* a_msg)
	{
		switch (a_msg->type) {
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
	SKSE::Init(a_skse, { .trampoline = true, .trampolineSize = 64 });  // inventory zoom call hooks
	logs::info("Item Inspection loading");

	Seen::Register();
	Inspect::InstallHooks();
	SKSE::GetMessagingInterface()->RegisterListener(OnMessage);
	return true;
}
