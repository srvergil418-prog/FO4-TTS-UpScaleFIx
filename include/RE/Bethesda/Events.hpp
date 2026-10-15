#pragma once
// "#undef near/far" below on EVERY inclusion, so that whichever copy is
// reached after <Windows.h> releases the names for the whole TU.

// Shim: TTS includes the ".hpp" spelling; powerof3/CommonLibF4 ships "RE/Bethesda/Events.h".
#include <RE/Bethesda/Events.h>
