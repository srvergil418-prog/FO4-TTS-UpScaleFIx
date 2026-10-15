#pragma once
// "#undef near/far" below on EVERY inclusion, so that whichever copy is
// reached after <Windows.h> releases the names for the whole TU.

// Shim: TTS includes the ".hpp" spelling; powerof3/CommonLibF4 ships "RE/Bethesda/BSAnimationGraph.h".
#include <RE/Bethesda/BSAnimationGraph.h>
