#include "Settings.h"

#include <Windows.h>

#include <filesystem>

namespace Settings
{
	namespace
	{
		Values values;

		// GetPrivateProfileString looks for relative paths in the Windows folder
		const std::wstring& Path()
		{
			static const std::wstring path = (std::filesystem::current_path() / "Data" / "SKSE" / "Plugins" / "ItemInspection.ini").wstring();
			return path;
		}

		std::wstring Wide(std::string_view a_text) { return { a_text.begin(), a_text.end() }; }

		std::wstring ReadString(std::string_view a_section, std::string_view a_key)
		{
			wchar_t buffer[64]{};
			GetPrivateProfileStringW(Wide(a_section).c_str(), Wide(a_key).c_str(), L"", buffer, static_cast<DWORD>(std::size(buffer)), Path().c_str());
			return buffer;
		}

		void WriteString(std::string_view a_section, std::string_view a_key, const std::string& a_value)
		{
			WritePrivateProfileStringW(Wide(a_section).c_str(), Wide(a_key).c_str(), Wide(a_value).c_str(), Path().c_str());
		}

		// one table for reading and writing every setting
		template <class F>
		void ForEach(Values& a_values, F&& a_func)
		{
			a_func("General", "bEnabled", a_values.enabled);
			a_func("General", "bAlwaysInspect", a_values.alwaysInspect);
			a_func("General", "bInventoryInspect", a_values.inventoryInspect);
			a_func("General", "bInspectStolen", a_values.inspectStolen);
			a_func("General", "bTelekinesis", a_values.telekinesis);
			a_func("General", "bTelekinesisLight", a_values.telekinesisLight);
			a_func("General", "bTelekinesisSound", a_values.telekinesisSound);
			a_func("General", "fTelekinesisSize", a_values.telekinesisSize, kEffectSize);
			a_func("General", "bSkipInCombat", a_values.skipInCombat);
			a_func("General", "bSkipGold", a_values.skipGold);
			a_func("General", "bSkipAmmo", a_values.skipAmmo);
			a_func("General", "bSkipWeapons", a_values.skipWeapons);
			a_func("General", "bSkipArmor", a_values.skipArmor);
			a_func("General", "bShowHint", a_values.showHint);
			a_func("General", "bWorldWaits", a_values.worldWaits);
			a_func("General", "bFadeTransition", a_values.fadeTransition);
			a_func("General", "fFadeTime", a_values.fadeTime, kFadeTime);
			a_func("General", "iLanguage", a_values.language);

			a_func("Controls", "iStoreKey", a_values.storeKey);
			a_func("Controls", "iPutBackKey", a_values.putBackKey);
			a_func("Controls", "iStoreGamepadKey", a_values.storeGamepadKey);
			a_func("Controls", "iPutBackGamepadKey", a_values.putBackGamepadKey);
			a_func("Controls", "iLookKey", a_values.lookKey);
			a_func("Controls", "iLookGamepadKey", a_values.lookGamepadKey);
			a_func("Controls", "bInvertLookY", a_values.invertLookY);
			a_func("Controls", "fMouseSensitivity", a_values.mouseSensitivity, kSensitivity);
			a_func("Controls", "fGamepadSensitivity", a_values.gamepadSensitivity, kSensitivity);
			a_func("Controls", "bInvertY", a_values.invertY);

			a_func("Hold", "fHoldRight", a_values.holdRight, kHoldRight);
			a_func("Hold", "fHoldForward", a_values.holdForward, kHoldForward);
			a_func("Hold", "fHoldUp", a_values.holdUp, kHoldUp);
			a_func("Hold", "fHoldHeight", a_values.holdHeight, kHoldHeight);
			a_func("Hold", "fItemRight", a_values.itemRight, kItemOffset);
			a_func("Hold", "fItemForward", a_values.itemForward, kItemOffset);
			a_func("Hold", "fItemUp", a_values.itemUp, kItemOffset);
			a_func("Hold", "fHandTurn", a_values.handTurn, kHandAngle);
			a_func("Hold", "fHandTilt", a_values.handTilt, kHandAngle);
			a_func("Hold", "fHandRoll", a_values.handRoll, kHandAngle);
			a_func("Hold", "fMaxItemSize", a_values.maxItemSize, kMaxItemSize);
			a_func("Weapons", "fWeaponRight", a_values.weaponRight, kHoldRight);
			a_func("Weapons", "fWeaponForward", a_values.weaponForward, kHoldForward);
			a_func("Weapons", "fWeaponUp", a_values.weaponUp, kHoldUp);
			a_func("Weapons", "fWeaponLeanLeft", a_values.weaponLeanLeft, kHandAngle);
			a_func("Weapons", "fWeaponLeanForward", a_values.weaponLeanForward, kHandAngle);
			a_func("Weapons", "fWeaponRoll", a_values.weaponRoll, kHandAngle);
			a_func("Bows", "fBowRight", a_values.bowRight, kHoldRight);
			a_func("Bows", "fBowForward", a_values.bowForward, kHoldForward);
			a_func("Bows", "fBowUp", a_values.bowUp, kHoldUp);
			a_func("Bows", "fBowLeanLeft", a_values.bowLeanLeft, kHandAngle);
			a_func("Bows", "fBowLeanForward", a_values.bowLeanForward, kHandAngle);
			a_func("Hold", "fRaiseTime", a_values.raiseTime, kTime);
			a_func("Hold", "fStowTime", a_values.stowTime, kTime);
			a_func("Hold", "fReturnTime", a_values.returnTime, kTime);
		}

		struct Reader
		{
			void operator()(std::string_view a_section, std::string_view a_key, bool& a_value) const
			{
				if (const auto text = ReadString(a_section, a_key); !text.empty()) {
					a_value = text != L"0" && text != L"false";
				}
			}
			void operator()(std::string_view a_section, std::string_view a_key, int& a_value) const
			{
				if (const auto text = ReadString(a_section, a_key); !text.empty()) {
					a_value = std::wcstol(text.c_str(), nullptr, 10);
				}
			}
			void operator()(std::string_view a_section, std::string_view a_key, float& a_value, Range a_range) const
			{
				if (const auto text = ReadString(a_section, a_key); !text.empty()) {
					a_value = std::clamp(std::wcstof(text.c_str(), nullptr), a_range.min, a_range.max);
				}
			}
		};

		struct Writer
		{
			void operator()(std::string_view a_section, std::string_view a_key, bool& a_value) const
			{
				WriteString(a_section, a_key, a_value ? "1" : "0");
			}
			void operator()(std::string_view a_section, std::string_view a_key, int& a_value) const
			{
				WriteString(a_section, a_key, std::to_string(a_value));
			}
			void operator()(std::string_view a_section, std::string_view a_key, float& a_value, Range) const
			{
				WriteString(a_section, a_key, std::format("{:.2f}", a_value));
			}
		};
	}

	const Values& Get()
	{
		return values;
	}

	Values& Edit()
	{
		return values;
	}

	void Load()
	{
		ForEach(values, Reader{});
		logs::info("Settings: enabled {}, always {}, inventory {}, keys store {} / {} put back {} / {}, hold ({}, {}, {}), max size {}",
			values.enabled, values.alwaysInspect, values.inventoryInspect, values.storeKey, values.storeGamepadKey, values.putBackKey,
			values.putBackGamepadKey, values.holdRight, values.holdForward, values.holdUp, values.maxItemSize);
	}

	void Save()
	{
		ForEach(values, Writer{});
	}
}
