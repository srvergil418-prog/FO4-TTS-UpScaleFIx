#pragma once

// ============================================================================
// OVERRIDE of the library's RE/NetImmerse/NiFrustum.h
// ----------------------------------------------------------------------------
// TTS's include/ directory precedes the library's on the compiler command line,
// so this file replaces powerof3's 18-line NiFrustum.h.
//
// WHY: the library names these two floats "near" and "far":
//
//        float near;    // 10
//        float far;     // 14
//
// <Windows.h> defines near and far as *EMPTY* macros, so on any translation
// unit where <Windows.h> is already in scope,
//
//        frustum.near = 15.0f;      becomes      frustum. = 15.0f;   -> C2059
//
// Trying to #undef them proved unreliable (the macro state kept coming back
// before the .cpp bodies), and no library source uses those member names at all
// -- verified across every .h/.cpp in powerof3/CommonLibF4. So the honest fix is
// to name the members what TTS actually writes, and to avoid those two
// macro-tainted identifiers entirely.
//
// LAYOUT IS IDENTICAL to the library's: same order, same offsets, same size.
// The static_assert below keeps that honest.
// ============================================================================

namespace RE
{
	class NiFrustum
	{
	public:
		float left;       // 00
		float right;      // 04
		float top;        // 08
		float bottom;     // 0C
		float nearPlane;  // 10  (library spelling: "near" -- unusable, <Windows.h> macro)
		float farPlane;   // 14  (library spelling: "far"  -- unusable, <Windows.h> macro)
		bool  ortho;      // 18
	};
	static_assert(sizeof(NiFrustum) == 0x1C);
}
