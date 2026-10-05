#pragma once
#include <algorithm>
#include <array>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <utility>
using u8 = uint8_t;
using u16 = uint16_t;
using u32 = uint32_t;
using vu8 = volatile u8;
using vu32 = volatile u32;
using TCHAR = char;
#define LOG_DEBUG(...) ((void)0)
#include "core/EnableSharedFromThis.h"
