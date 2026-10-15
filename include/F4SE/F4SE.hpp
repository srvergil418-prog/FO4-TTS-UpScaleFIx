// NOTE: no #pragma once on purpose -- this header must re-run the
// "#undef near/far" below on EVERY inclusion, so that whichever copy is
// reached after <Windows.h> releases the names for the whole TU.
#include <tts-undef-winmacros.h>

#ifndef TTS_SHIM_F4SE_HPP
#define TTS_SHIM_F4SE_HPP
// Shim: TTS 的 src/PCH.h 用 #include "F4SE/F4SE.hpp"（.hpp），
// 而 powerof3/CommonLibF4 提供的是 F4SE/F4SE.h（.h），扩展名不同无法互相解析。
//
// powerof3 版本已聚合 TTS 需要的全部 F4SE:: API：
//   PluginInfo / LoadInterface / QueryInterface / PluginVersionData /
//   AllocTrampoline / MessagingInterface / RUNTIME_1_10_163 / log /
//   F4SE_EXPORT / F4SEAPI / AuthorName / CompatibleVersions 等
#include <F4SE/F4SE.h>

namespace F4SE
{
	// powerof3/CommonLibF4 未定义 VR 专用的 RUNTIME_LATEST_VR，
	// 但 TTS 的 main.cpp 在 REL::Module::IsVR() 分支里引用了它。
	// 桌面版永远走不到那个分支，这里补一个占位常量即可。
#ifndef TTS_RUNTIME_LATEST_VR_DEFINED
#define TTS_RUNTIME_LATEST_VR_DEFINED
	inline constexpr REL::Version RUNTIME_LATEST_VR{ 1, 2, 72, 0 };
#endif
}
#endif  // TTS_SHIM_F4SE_HPP
