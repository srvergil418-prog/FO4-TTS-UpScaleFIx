#pragma once
// TTS src/D3DHooks.cpp does:  #include <detours.h>
// but the vcpkg detours package installs its header one level deeper:
//   <vcpkg>/include/detours/detours.h
// so the flat spelling cannot resolve.
//
// The author's .gitignore contains "/include/detours.h", which confirms he kept
// his own copy in include/ and never committed it.
// This forwarding header closes the gap (the vcpkg include dir is already on the
// compiler command line).
#include <detours/detours.h>
