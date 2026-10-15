#pragma once
// Shim for TTS's "#include \"RE/Bethesda/BSGraphics.hpp\"" (.hpp spelling).
// powerof3/CommonLibF4 ships BSGraphics.h, but lacks two enums that TTS's
// HookManager.h references. SetRenderTargetMode is ported from
// libxse/commonlibf4@main: include/RE/B/BSGraphics.h
#include <RE/Bethesda/BSGraphics.h>

namespace RE
{
	namespace BSGraphics
	{
		// ported from libxse/commonlibf4@main
		enum class SetRenderTargetMode : std::int32_t
		{
			kClear = 0x0,
			kClearDepth = 0x1,
			kClearStencil = 0x2,
			kNoClear = 0x3,
			kRestore = 0x3,
			kForceCopyRestore = 0x4,
			kInit = 0x5
		};

		// TTS only ever receives this as a hook parameter (never constructs it),
		// so the exact member values do not affect behaviour.
		enum class TARGET_PERSISTENCY : std::int32_t
		{
			kNone = 0x0,
			kPosition = 0x1,
			kColor = 0x2,
			kNormals = 0x3,
			kDepth = 0x4,
			kStencil = 0x5,
			kCount
		};
	}

	// TTS 的 HookManager.h 用的是裸名（不带 BSGraphics:: 前缀），
	// 这里再把两个名字导出到 RE:: 下，保证两种写法都能解析。
	using BSGraphics::SetRenderTargetMode;
	using BSGraphics::TARGET_PERSISTENCY;
}
