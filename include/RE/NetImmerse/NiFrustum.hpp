#pragma once
// Shim for TTS's "#include \"RE/NetImmerse/NiFrustum.hpp\"" (.hpp spelling).
//
// powerof3/CommonLibF4 ships NiFrustum.h (.h) and does NOT define
// NiPlane / NiFrustumPlanes at all, which TTS's ScopeCulling needs.
//
// The LAYOUT (NiPlane 0x10, NiFrustumPlanes 0x70) is ported from
// libxse/commonlibf4@main: include/RE/N/NiPlane.h, include/RE/N/NiFrustumPlanes.h
// but that revision uses different member names. TTS instead uses the
// m_kNormal / m_fConstant / m_akCullingPlanes / m_uiActivePlanes spelling,
// so the names below follow TTS. The byte layout is unchanged.
//
// NiFrustumPlanes::Set(const NiCamera*) is declared here but DEFINED in the
// build shim (see the PCH patch step), because its body needs a complete
// NiCamera type and NiCamera.h includes NiFrustum.h.
#include <RE/NetImmerse/NiFrustum.h>
#include <RE/NetImmerse/NiPoint.h>

namespace RE
{
	class NiCamera;

	class NiPlane
	{
	public:
		// members
		NiPoint3 m_kNormal;    // 00
		float    m_fConstant;  // 0C
	};
	static_assert(sizeof(NiPlane) == 0x10);

	class NiFrustumPlanes
	{
	public:
		enum : std::uint32_t
		{
			kNearPlane = 0x0,
			kFarPlane = 0x1,
			kLeftPlane = 0x2,
			kRightPlane = 0x3,
			kTopPlane = 0x4,
			kBottomPlane = 0x5,
			kMax = 0x6
		};

		// Fill all six planes from a camera's world transform + view frustum.
		// Declared here, defined in the build shim once NiCamera is complete.
		void Set(const NiCamera* a_camera);

		[[nodiscard]] NiPlane&       GetPlane(std::uint32_t a_index) { return m_akCullingPlanes[a_index]; }
		[[nodiscard]] const NiPlane& GetPlane(std::uint32_t a_index) const { return m_akCullingPlanes[a_index]; }

		[[nodiscard]] bool IsPlaneActive(std::uint32_t a_index) const
		{
			return (m_uiActivePlanes & (1u << a_index)) != 0;
		}

		void EnableAllPlanes() { m_uiActivePlanes = 0x3F; }

		// members
		NiPlane       m_akCullingPlanes[6];  // 00
		std::uint32_t m_uiActivePlanes;      // 60
		std::uint32_t m_uiBasePlaneStates;   // 64
		std::byte     _pad[0x8];             // 68
	};
	static_assert(sizeof(NiFrustumPlanes) == 0x70);
}
