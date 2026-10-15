#pragma once
// Shim for TTS's "#include \"RE/Bethesda/BSGraphics.hpp\"" (.hpp spelling).
//
// powerof3/CommonLibF4 ships BSGraphics.h (.h) but lacks two enums that TTS's
// HookManager.h references by BARE name (no BSGraphics:: prefix), so they have
// to be visible directly under RE::.
//
// The build shim defines them from the PCH (which is expanded before every TTS
// header) because HookManager.h never includes this file. The shared include
// guard below keeps this header usable on its own without clashing with the PCH.
#include <RE/Bethesda/BSGraphics.h>

#ifndef TTS_BUILD_SHIM_BSGRAPHICS_ENUMS
#define TTS_BUILD_SHIM_BSGRAPHICS_ENUMS
namespace RE
{
	namespace BSGraphics
	{
		// ported from libxse/commonlibf4@main (RE/B/BSGraphics.h)
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

		// Only ever received as a hook parameter; TTS never constructs one, so the
		// specific enumerator values carry no behavioural risk.
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

	// Export under RE:: as well, so the bare names used by TTS resolve either way.
	using BSGraphics::SetRenderTargetMode;
	using BSGraphics::TARGET_PERSISTENCY;
}
#endif
