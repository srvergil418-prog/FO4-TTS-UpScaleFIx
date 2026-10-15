// NOTE: no #pragma once on purpose -- this header must re-run the
// "#undef near/far" below on EVERY inclusion, so that whichever copy is
// reached after <Windows.h> releases the names for the whole TU.
#include <tts-undef-winmacros.h>

// Shim: TTS includes the ".hpp" spelling; powerof3/CommonLibF4 ships "RE/Bethesda/Sky.h".
#include <RE/Bethesda/Sky.h>
