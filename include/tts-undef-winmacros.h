// ============================================================================
// tts-undef-winmacros.h
// ----------------------------------------------------------------------------
// DELIBERATELY HAS NO INCLUDE GUARD.
//
// <Windows.h> defines `near` and `far` as *EMPTY* macros. TTS's sources write
//
//     m_scopeFrustum.near = 15.0f;
//
// which therefore expands to
//
//     m_scopeFrustum.     = 15.0f;      -> error C2059
//
// The library's own NiFrustum.h survives "float near;" only because it happens
// to be included BEFORE <Windows.h>.
//
// #undef only helps if it runs AFTER <Windows.h> has been pulled in. Since we
// cannot know which of our headers is reached last, every forwarding header
// includes this file, and because this file has no guard the #undef runs on
// EVERY inclusion. As soon as one of them is reached after <Windows.h>, the
// names are released for the rest of the translation unit.
//
// The accompanying #define near is intentionally absent: after the #undef,
// `near` and `far` are ordinary member names of RE::NiFrustum again.
// ============================================================================

#ifdef near
#undef near
#endif

#ifdef far
#undef far
#endif
