// ============================================================================
// build shim -- appended verbatim to True-Through-Scope/src/PCH.h by ci/build.ps1
// ----------------------------------------------------------------------------
// Everything below adapts TTS's source to powerof3/CommonLibF4, whose API
// differs from the (private) library the author used. Appending to PCH.h means
// it is expanded before every TTS header, so these declarations are always
// visible without editing a single TTS source file.
//
// !! Items marked INFERRED are reconstructed from TTS's usage, not from a
// !! binary-verified definition.
// ============================================================================

#include <RE/NetImmerse/NiFrustum.hpp>
#include <RE/NetImmerse/NiCamera.h>
#include <RE/NetImmerse/NiStream.h>
#include <RE/NetImmerse/NiShadeProperty.h>
#include <RE/NetImmerse/NiTLargeArray.h>

// ---------------------------------------------------------------------------
// Forward declarations for types absent from powerof3/CommonLibF4.
// ---------------------------------------------------------------------------
namespace RE
{
	class BSEffectShaderProperty;
	class BSGeometryListCullingProcess;
	class BSCullingProcess;

	namespace BSGraphics
	{
		class DynamicTriShape;
		class DynamicTriShapeDrawData;
	}

	// ------------------------------------------------------------------------
	// BSLight
	// No public CommonLibF4 revision defines this class (powerof3 and libxse
	// both only forward-declare it), yet TTS needs:
	//   (a) NiPointer<BSLight> to be destructible, and
	//   (b) seven members, used by LightBackupSystem to save/restore indoor lights.
	//
	// !! INFERRED LAYOUT -- NOT BINARY-VERIFIED !!
	// The member order follows the order TTS touches them. If the offsets are
	// wrong the consequence is inaccurate light backup/restore (a wrong POD
	// value is read and written back), NOT a crash: the base chain
	// (NiAVObject -> NiObject -> NiRefObject) supplies the virtual destructor,
	// so the NiPointer teardown path stays valid.
	// ------------------------------------------------------------------------
	class BSLight : public NiAVObject
	{
	public:
		std::uint16_t       usFrustumCull;    // inferred
		bool                bOccluded;        // inferred
		bool                bTemporary;       // inferred
		bool                bDynamicLight;    // inferred
		float               fLODDimmer;       // inferred
		NiPointer<NiCamera> spCamera;         // inferred
		BSCullingProcess*   pCullingProcess;  // inferred

		// --- setters called by LightBackupSystem::RestoreLightState ---
		void SetOccluded(bool a_v) { bOccluded = a_v; }
		void SetTemporary(bool a_v) { bTemporary = a_v; }
		void SetDynamic(bool a_v) { bDynamicLight = a_v; }
		void SetCullingProcess(BSCullingProcess* a_v) { pCullingProcess = a_v; }
		void SetLODFade(bool a_v) { fLODDimmer = a_v ? 1.0f : 0.0f; }

		// The five flag setters below would act on the engine's lightFlags
		// bitfield, whose bit layout is unknown here. They are deliberately
		// no-ops instead of guesses: writing the wrong bit would corrupt real
		// light state, whereas a no-op only leaves this flag unrestored.
		void SetAffectLand(bool) {}
		void SetAffectWater(bool) {}
		void SetIgnoreRoughness(bool) {}
		void SetIgnoreRim(bool) {}
		void SetAttenuationOnly(bool) {}
	};

	// ------------------------------------------------------------------------
	// BSEffectShaderProperty
	// powerof3 only has the *Controller* variants. TTS uses this type for an
	// RTTI id comparison and a static_cast, and the consuming routine
	// (ProcessBSEffectShaderProperty) is an empty placeholder, so merely having
	// the type is enough -- it is never instantiated.
	// ------------------------------------------------------------------------
	class BSEffectShaderProperty : public NiShadeProperty {};

	// ------------------------------------------------------------------------
	// BSStream
	// powerof3 calls this class NiStream. TTS uses the newer name and also calls
	// the static factory BSStream::Create(), which NiStream lacks.
	// ------------------------------------------------------------------------
	class BSStream : public NiStream
	{
	public:
		static BSStream* Create() { return new BSStream(); }
	};
}

// ---------------------------------------------------------------------------
// REL::RelocationID(aeID, ngID) -- newer CommonLibF4 API carrying both the AE
// and NG Address Library IDs. powerof3 only has the single-ID REL::ID.
// This build targets 1.10.163, so the first argument is the one to use.
// ---------------------------------------------------------------------------
namespace REL
{
	class RelocationID
	{
	public:
		constexpr RelocationID(std::uint64_t a_aeID, std::uint64_t) noexcept :
			_id{ a_aeID }
		{}

		[[nodiscard]] constexpr operator ID() const noexcept { return ID{ _id }; }

	private:
		std::uint64_t _id;
	};
}

// ===========================================================================
// A-class API differences: names / constants only, no layout guessing.
// ===========================================================================

// ---------------------------------------------------------------------------
// TARGET_PERSISTENCY / SetRenderTargetMode.
// HookManager.h uses both by bare name but never includes BSGraphics.hpp, so
// they have to come from the PCH.
// ---------------------------------------------------------------------------
#ifndef TTS_BUILD_SHIM_BSGRAPHICS_ENUMS
#define TTS_BUILD_SHIM_BSGRAPHICS_ENUMS
namespace RE
{
	namespace BSGraphics
	{
		// ported from libxse/commonlibf4@main (RE/B/BSGraphics.h)
		enum class SetRenderTargetMode : std::int32_t
		{
			kClear = 0x0,
			kClearDepth = 0x1,
			kClearStencil = 0x2,
			kNoClear = 0x3,
			kRestore = 0x3,
			kForceCopyRestore = 0x4,
			kInit = 0x5
		};

		// Only ever received as a hook parameter; TTS never constructs one.
		enum class TARGET_PERSISTENCY : std::int32_t
		{
			kNone = 0x0,
			kPosition = 0x1,
			kColor = 0x2,
			kNormals = 0x3,
			kDepth = 0x4,
			kStencil = 0x5,
			kCount
		};
	}

	using BSGraphics::SetRenderTargetMode;
	using BSGraphics::TARGET_PERSISTENCY;
}
#endif

// ---------------------------------------------------------------------------
// LargeArrayAt
// powerof3's NiTLargeArray carries the engine layout (vtable at 0x00, _data at
// 0x08 -- documented in the header itself) but exposes no accessors, while
// TTS's NiFLoader needs stream->topObjects[0].
// ---------------------------------------------------------------------------
namespace RE
{
	namespace build_shim
	{
		template <class T, class Allocator>
		[[nodiscard]] T& LargeArrayAt(NiTLargeArray<T, Allocator>& a_array, std::uint32_t a_index)
		{
			auto* base = reinterpret_cast<std::byte*>(&a_array);
			auto* data = *reinterpret_cast<T**>(base + 0x08);
			return data[a_index];
		}
	}
}

// ---------------------------------------------------------------------------
// NiFrustumPlanes::Set(const NiCamera*)
// powerof3 has no such type at all, so the engine implementation is
// unavailable. This deliberately does NOT call the author's RELID 1361032:
// that ID comes from his private Address-Library table and would resolve to an
// unrelated function in the 1.10.163 table (calling it would crash).
//
// Instead the six planes are extracted directly, using the same axis convention
// and cross-product order as TTS's own RebuildCachedPlanes.
//   world rows: 0 = forward, 1 = up, 2 = right
// ---------------------------------------------------------------------------
namespace RE
{
	inline void NiFrustumPlanes::Set(const NiCamera* a_camera)
	{
		if (!a_camera) { return; }

		const auto& world = a_camera->world;
		const auto& frust = a_camera->viewFrustum;

		const NiPoint3 forward{ world.rotate.entry[0][0], world.rotate.entry[0][1], world.rotate.entry[0][2] };
		const NiPoint3 up{ world.rotate.entry[1][0], world.rotate.entry[1][1], world.rotate.entry[1][2] };
		const NiPoint3 right{ world.rotate.entry[2][0], world.rotate.entry[2][1], world.rotate.entry[2][2] };
		const NiPoint3& camPos = world.translate;

		auto cross = [](const NiPoint3& a, const NiPoint3& b) {
			return NiPoint3{ a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x };
		};
		auto normalize = [](NiPoint3 v) {
			const float len = std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
			if (len > 0.0001f) { v.x /= len; v.y /= len; v.z /= len; }
			return v;
		};
		// forward*near + axis*extent -- the edge direction of one frustum side.
		auto toward = [&](const NiPoint3& a_axis, float a_extent) {
			return NiPoint3{ forward.x * frust.near + a_axis.x * a_extent,
							 forward.y * frust.near + a_axis.y * a_extent,
							 forward.z * frust.near + a_axis.z * a_extent };
		};
		auto onPoint = [&](NiPlane& a_plane, const NiPoint3& a_normal, const NiPoint3& a_point) {
			a_plane.m_kNormal = a_normal;
			a_plane.m_fConstant = a_normal.x * a_point.x + a_normal.y * a_point.y + a_normal.z * a_point.z;
		};

		const NiPoint3 nearPoint{ camPos.x + forward.x * frust.near, camPos.y + forward.y * frust.near, camPos.z + forward.z * frust.near };
		const NiPoint3 farPoint{ camPos.x + forward.x * frust.far, camPos.y + forward.y * frust.far, camPos.z + forward.z * frust.far };
		const NiPoint3 negForward{ -forward.x, -forward.y, -forward.z };

		onPoint(m_akCullingPlanes[kNearPlane], forward, nearPoint);
		onPoint(m_akCullingPlanes[kFarPlane], negForward, farPoint);
		onPoint(m_akCullingPlanes[kLeftPlane], normalize(cross(toward(right, frust.left), up)), camPos);
		onPoint(m_akCullingPlanes[kRightPlane], normalize(cross(up, toward(right, frust.right))), camPos);
		onPoint(m_akCullingPlanes[kTopPlane], normalize(cross(toward(up, frust.top), right)), camPos);
		onPoint(m_akCullingPlanes[kBottomPlane], normalize(cross(right, toward(up, frust.bottom))), camPos);

		EnableAllPlanes();
	}
}

// ===========================================================================
// B-class: engine types that NO public CommonLibF4 revision defines.
//
// Sources of confidence, per type:
//   * Shape (base class, member count/order) is taken from how TTS uses it.
//   * Where powerof3 has an EQUIVALENT structure with known offsets, we copy
//     those offsets instead of inventing them (see RendererShadowState, which
//     is patched in ci/build.ps1 and mirrors CameraStateData).
//
// TTS only WRITES to the scalar members below (never reads them back), so a
// wrong offset degrades a light/FOV/culling setting rather than corrupting a
// pointer. Methods are implemented as member writes or no-ops -- none of them
// call an engine address, because swapping REL IDs across game versions means
// calling an unrelated function.
// ===========================================================================
namespace RE
{
	// ------------------------------------------------------------------------
	// NiPoint3A -- a 16-byte-aligned NiPoint3. TTS uses it for eye positions.
	// ------------------------------------------------------------------------
	struct alignas(0x10) NiPoint3A : NiPoint3
	{
	};
	static_assert(sizeof(NiPoint3A) == 0x10);

	// ------------------------------------------------------------------------
	// ShadowSceneNode -- TTS passes it where a NiNode* is expected
	// (SecondPassRenderer.cpp:531), so it must derive from NiNode.
	// Members written by TTS: light-update control + stored far clip.
	// ------------------------------------------------------------------------
	class ShadowSceneNode : public NiNode
	{
	public:
		NiPoint3A kEyePosition;          // inferred
		bool      bDisableLightUpdate;   // inferred
		float     fStoredFarClip;        // inferred
		bool      bAlwaysUpdateLights;   // inferred
	};

	// ------------------------------------------------------------------------
	// BSShaderAccumulator -- TTS sets the eye position and camera on it, and
	// calls ClearActivePasses() once right before re-pointing everything.
	// ------------------------------------------------------------------------
	class BSShaderAccumulator
	{
	public:
		NiPoint3A kEyePosition;   // inferred
		NiCamera* m_pkCamera;     // inferred

		// The engine clears its internal pass list here. The real function is
		// unavailable, and TTS calls it immediately before it re-assigns the
		// camera, so a no-op is the safe approximation.
		void ClearActivePasses(bool) {}
	};

	// ------------------------------------------------------------------------
	// BSGeometryListCullingProcess -- TTS gives it the scope camera and frustum.
	// ------------------------------------------------------------------------
	class BSGeometryListCullingProcess
	{
	public:
		NiCamera* m_pkCamera;   // inferred
		NiFrustum m_kFrustum;   // inferred

		void SetFrustum(const NiFrustum* a_frustum)
		{
			if (a_frustum) { m_kFrustum = *a_frustum; }
		}
	};

	// ------------------------------------------------------------------------
	// BSCullingProcess -- powerof3 only forward-declares it (SceneGraph.h), but
	// TTS reads/writes bCustomCullPlanes and calls SetCustomCullPlanes().
	// ------------------------------------------------------------------------
	class BSCullingProcess
	{
	public:
		bool            bCustomCullPlanes;   // inferred
		NiFrustumPlanes m_kPlanes;           // inferred (target of SetCustomCullPlanes)

		void SetCustomCullPlanes(const NiFrustumPlanes* a_planes)
		{
			if (a_planes) { m_kPlanes = *a_planes; bCustomCullPlanes = true; }
		}
	};

	// ------------------------------------------------------------------------
	// BSShaderManagerState -- TTS only uses it as a Relocation<BSShaderManagerState**>
	// template argument and never dereferences members. An opaque tag is enough.
	// ------------------------------------------------------------------------
	class BSShaderManagerState;
}

// ===========================================================================
// CRITICAL -- must stay at the very END of the appended block.
//
// <Windows.h> defines near and far as *EMPTY* macros. TTS's sources write
//
//     frustum.near = 15.0f;
//
// which therefore expands to
//
//     frustum.      = 15.0f;      -> error C2059
//
// (The library's own NiFrustum.h survives "float near;" only because it is
//  included before <Windows.h>.)
//
// Because this shim is appended to the end of PCH.h, this is reached AFTER
// <Windows.h> has been pulled in -- the only point where the #undef can help.
// ===========================================================================
#ifdef near
#undef near
#endif
#ifdef far
#undef far
#endif
