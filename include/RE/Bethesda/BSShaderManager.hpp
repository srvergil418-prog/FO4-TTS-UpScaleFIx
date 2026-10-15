// NOTE: no #pragma once on purpose -- this header must re-run the
// "#undef near/far" below on EVERY inclusion, so that whichever copy is
// reached after <Windows.h> releases the names for the whole TU.
#include <tts-undef-winmacros.h>

#ifndef TTS_SHIM_BSSHADERMANAGER_HPP
#define TTS_SHIM_BSSHADERMANAGER_HPP
// powerof3/CommonLibF4 has no BSShaderManager at all: the directional ambient
// colours were refactored into Sky (Sky.h  NiColor directionalAmbientColorsA[3][2]).
// This shim re-creates the old accessors on top of Sky, and adds the
// BSShaderManagerState tag type that TTS only uses as a Relocation template arg.
#include <RE/Bethesda/Sky.h>

namespace RE
{
	// TTS uses this only as  REL::Relocation<BSShaderManagerState**>  — it is
	// never instantiated or dereferenced through member access, so an opaque
	// declaration is sufficient and carries no layout risk.
	class BSShaderManagerState;

	class BSShaderManager
	{
	public:
		// Corresponds to Sky::directionalAmbientColorsA[3][2]
		[[nodiscard]] static NiColor (*GetDirectionalAmbientColorsA())[2]
		{
			auto* sky = Sky::GetSingleton();
			return sky ? sky->directionalAmbientColorsA : nullptr;
		}

		// Not present in the powerof3 layout; TTS guards every use with a null
		// check, so returning nullptr is a safe no-op.
		[[nodiscard]] static NiColor (*GetLocalDirectionalAmbientColorsA())[2]
		{
			return nullptr;
		}
	};
}
#endif  // TTS_SHIM_BSSHADERMANAGER_HPP
