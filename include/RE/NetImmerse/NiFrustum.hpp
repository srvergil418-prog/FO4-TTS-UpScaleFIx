#pragma once
// Shim for TTS's "#include \"RE/NetImmerse/NiFrustum.hpp\"" (.hpp spelling).
// powerof3/CommonLibF4 ships NiFrustum.h (.h) and does NOT define
// NiPlane / NiFrustumPlanes, which TTS's ScopeCulling needs.
// Those two classes are ported verbatim (layout + static_assert) from
// libxse/commonlibf4@main: include/RE/N/NiPlane.h, include/RE/N/NiFrustumPlanes.h
#include <RE/NetImmerse/NiFrustum.h>
#include <RE/NetImmerse/NiPoint.h>

namespace RE
{
	class NiPlane
	{
	public:
		// members
		NiPoint3 normal;    // 00
		float    constant;  // 0C
	};
	static_assert(sizeof(NiPlane) == 0x10);

	class NiFrustumPlanes
	{
	public:
		enum class Plane
		{
			kNearPlane = 0x0,
			kFarPlane = 0x1,
			kLeftPlane = 0x2,
			kRightPlane = 0x3,
			kTopPlane = 0x4,
			kBottomPlane = 0x5
		};

		// members
		NiPlane       cullingPlanes[6];  // 00
		std::uint32_t activePlanes;      // 60
		std::uint32_t basePlaneStates;   // 64
		std::byte     pad[0x8];          // 68
	};
	static_assert(sizeof(NiFrustumPlanes) == 0x70);
}
