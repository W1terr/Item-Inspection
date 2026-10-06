#pragma once

#define NOMINMAX
#define WIN32_LEAN_AND_MEAN

#include <RE/Skyrim.h>
#include <SKSE/SKSE.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <format>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>

namespace logs = SKSE::log;
using namespace std::literals;

#undef GetObject
#undef PlaySound
