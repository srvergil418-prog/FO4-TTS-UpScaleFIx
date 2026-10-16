#pragma once
// "#undef near/far" below on EVERY inclusion, so that whichever copy is
// reached after <Windows.h> releases the names for the whole TU.

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
		//
		// NOTE: this deliberately does NOT call Sky::GetSingleton() any more.
		//
		// powerof3 refactored these directional ambient colours into RE::Sky, but
		// Sky::GetSingleton() only exists in the master tree, and its address-library
		// ID there is 2192448 -- which is part of the NEWER F4SE/AddressLibrary ID
		// family. The whole 2192000-2193000 range is absent from the 1.10.163
		// database (verified: zero IDs in [2192400, 2192550]), and the dev-1.10.163
		// branch ships no Sky.h at all, so there is no correct value to be mapped to.
		// Calling it traps in IDDB with "id 2192448 not found!" -- observed exactly
		// when aiming down sights, i.e. the first time this shim is reached.
		//
		// Returning nullptr is a safe no-op rather than a second kind of crash:
		// TTS null-checks every consumer of both accessors
		// (LightBackupSystem.cpp 146/154, 187/195, 224/232), and the sibling
		// accessor just below has been returning nullptr for exactly this reason all
		// along. The consequence is a known, bounded regression: the directional
		// ambient COLOUR arrays are no longer backed up/restored, while the
		// transforms and specular parameters carried in BSShaderManagerState still are.
		[[nodiscard]] static NiColor (*GetDirectionalAmbientColorsA())[2]
		{
			return nullptr;
		}

		// Not present in the powerof3 layout; TTS guards every use with a null
		// check, so returning nullptr is a safe no-op.
		[[nodiscard]] static NiColor (*GetLocalDirectionalAmbientColorsA())[2]
		{
			return nullptr;
		}
	};
}
