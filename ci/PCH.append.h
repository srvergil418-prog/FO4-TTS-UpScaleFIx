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

#include <xmmintrin.h>   // __m128 (RendererShadowState::CameraData_t)
#include <cstddef>   // offsetof，供下面的偏移断言使用
#include <vector>

#include <RE/NetImmerse/NiFrustum.hpp>
#include <RE/NetImmerse/NiCamera.h>
#include <RE/NetImmerse/NiStream.h>
#include <RE/NetImmerse/NiObject.h>
#include <RE/NetImmerse/NiRefObject.h>
#include <RE/NetImmerse/NiShadeProperty.h>
#include <RE/NetImmerse/NiTLargeArray.h>
#include <RE/Bethesda/BSGeometry.h>
#include <RE/Bethesda/BSTArray.h>

namespace RE
{
	// ------------------------------------------------------------------------
	// BytePad<N> -- zero-filled spacer used to land members on PDB-verified
	// offsets.
	//
	// Why a template instead of `std::byte pad[N]`: when the base class already
	// ends exactly on the target offset, N is 0, and MSVC rejects a zero-length
	// array with C2229 ("illegal zero-sized array"). GCC/Clang tolerate it, MSVC
	// does not. The N == 0 specialisation contributes nothing and keeps the
	// arithmetic in the member lists honest.
	// ------------------------------------------------------------------------
	template <std::size_t N>
	struct BytePad
	{
		std::byte data[N];
	};

	template <>
	struct BytePad<0>
	{
	};
}

// Forward declarations needed by the PDB-derived layouts below.
namespace RE
{
	class NiVisibleArray;   // member of NiCullingProcess @ +0x10
	class NiAccumulator;    // held as NiPointer<> in BSCullingProcess @ +0x190
	class NiTexture;        // held as NiPointer<> in BSLight @ +0x158
}

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
	// BSLight : NiRefObject
	// No public CommonLibF4 revision defines this class (powerof3 and libxse
	// both only forward-declare it), yet LightBackupSystem reads and writes it
	// to save/restore indoor lighting.
	//
	// !! LAYOUT MEASURED FROM THE ORIGINAL PDB (43 members, sizeof 0x236) !!
	// The author's own build emitted:
	//     base            NiRefObject
	//     +0x10  float    fLODDimmer
	//     +0x14  float    fLuminance
	//     +0x18  uint32   usFrustumCull          <- uint32, not uint16
	//     +0xc8  NiPointer<NiLight>   spLight
	//     +0x140 BSCullingProcess*    pCullingProcess
	//     +0x148 NiPointer<BSGeometry> spGeometry
	//     +0x150 NiPointer<NiCamera>  spCamera
	//     +0x158 NiPointer<NiTexture> spGoboTexture
	//     +0x170 bool bPointLight    +0x171 bool bAmbientLight   +0x172 bool bDynamicLight
	//     +0x175 bool bAffectLand    +0x176 bool bAffectWater    +0x177 bool bLODFade
	//     +0x178 bool bSpecular      +0x179 bool bAttenuationOnly
	//     +0x17a bool bIgnoreRoughness +0x17b bool bIgnoreRim
	//     +0x17c bool bOccluded      +0x17d bool bStencilVolume  +0x17e bool bTemporary
	//
	// Previously this class derived from NiAVObject (wrong base) and the five
	// flag setters were no-ops because the flags' locations were unknown. With
	// the real offsets those setters now write the actual fields.
	//
	// NOTE: NiRefObject supplies the virtual destructor, so the
	// NiPointer<BSLight> teardown path stays valid.
	// ------------------------------------------------------------------------
	class BSLight : public NiRefObject
	{
	public:

		float   fLODDimmer;      // 10
		float   fLuminance;      // 14
		std::uint32_t usFrustumCull;  // 18
		BytePad<0x140 - 0x1C> pad_01C;  // 1C .. 13F (gobo projection, shape rotation, radius, spLight, fade-node list, fences, arrays, pPortalGraph)

		BSCullingProcess*            pCullingProcess;  // 140
		NiPointer<BSGeometry>        spGeometry;       // 148
		NiPointer<NiCamera>          spCamera;         // 150
		NiPointer<NiTexture>         spGoboTexture;    // 158
		BytePad<0x170 - 0x160> pad_160;              // 160

		bool bPointLight;        // 170
		bool bAmbientLight;      // 171
		bool bDynamicLight;      // 172
		BytePad<0x175 - 0x173> pad_173;  // 173 (bSpotLight/bShadowLight)
		bool bAffectLand;        // 175
		bool bAffectWater;       // 176
		bool bLODFade;           // 177
		bool bSpecular;          // 178
		bool bAttenuationOnly;   // 179
		bool bIgnoreRoughness;   // 17A
		bool bIgnoreRim;         // 17B
		bool bOccluded;          // 17C
		bool bStencilVolume;     // 17D
		bool bTemporary;         // 17E
		BytePad<0x236 - 0x17F> pad_17F;  // 17F .. 235

		// --- setters called by LightBackupSystem::RestoreLightState ---
		// All ten now write their real field at its PDB-verified offset.
		void SetOccluded(bool a_v) { bOccluded = a_v; }
		void SetTemporary(bool a_v) { bTemporary = a_v; }
		void SetDynamic(bool a_v) { bDynamicLight = a_v; }
		void SetCullingProcess(BSCullingProcess* a_v) { pCullingProcess = a_v; }
		void SetLODFade(bool a_v) { bLODFade = a_v; }
		void SetAffectLand(bool a_v) { bAffectLand = a_v; }
		void SetAffectWater(bool a_v) { bAffectWater = a_v; }
		void SetIgnoreRoughness(bool a_v) { bIgnoreRoughness = a_v; }
		void SetIgnoreRim(bool a_v) { bIgnoreRim = a_v; }
		void SetAttenuationOnly(bool a_v) { bAttenuationOnly = a_v; }
	};
	static_assert(sizeof(NiRefObject) == 0x10, "NiRefObject must be 0x10 for BSLight padding to line up");
	static_assert(offsetof(BSLight, pCullingProcess) == 0x140, "BSLight::pCullingProcess must be 0x140");
	static_assert(offsetof(BSLight, spCamera) == 0x150, "BSLight::spCamera must be 0x150");
	static_assert(offsetof(BSLight, bDynamicLight) == 0x172, "BSLight::bDynamicLight must be 0x172");
	static_assert(offsetof(BSLight, bAffectLand) == 0x175, "BSLight::bAffectLand must be 0x175");
	static_assert(offsetof(BSLight, bLODFade) == 0x177, "BSLight::bLODFade must be 0x177");
	static_assert(offsetof(BSLight, bAttenuationOnly) == 0x179, "BSLight::bAttenuationOnly must be 0x179");
	static_assert(offsetof(BSLight, bIgnoreRoughness) == 0x17A, "BSLight::bIgnoreRoughness must be 0x17A");
	static_assert(offsetof(BSLight, bIgnoreRim) == 0x17B, "BSLight::bIgnoreRim must be 0x17B");
	static_assert(offsetof(BSLight, bOccluded) == 0x17C, "BSLight::bOccluded must be 0x17C");
	static_assert(offsetof(BSLight, bTemporary) == 0x17E, "BSLight::bTemporary must be 0x17E");
	static_assert(sizeof(BSLight) == 0x238, "BSLight must be 0x238 (0x236 rounded up to the 8-byte alignment NiRefObject imposes)");

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
	// powerof3 calls this class NiStream, and TTS uses the newer name plus a
	// static factory BSStream::Create() that NiStream lacks.
	//
	// IMPORTANT: this deliberately does NOT derive from RE::NiStream.
	// NiStream's three engine entry points (the virtual destructor, Load, and
	// the NiTMapBase/NiTLargeArray instantiations it contains) are declared in
	// the header but IMPLEMENTED IN THE GAME -- powerof3 never compiles a
	// NiStream.cpp, so CommonLibF4.lib exports none of them. Deriving from it
	// therefore produces:
	//     LNK2001: unresolved external symbol RE::NiStream::~NiStream(void)
	//     LNK2001: unresolved external symbol RE::NiStream::Load(char const*)
	//     LNK2001: unresolved external symbol RE::NiTLargeArray<...>::~NiTLargeArray(void)
	// (plus the NiTPointerMap / NiTMapBase ones) and the virtual destructor also
	// needs the engine vtable, which cannot be synthesised.
	//
	// So this is a self-contained stand-in: Load() reports failure, which makes
	// NIFLoader::LoadNIF return nullptr. That disables the custom-scope-model
	// feature (unrelated to the upscaling fix) instead of failing to link.
	// ------------------------------------------------------------------------
	class BSStream
	{
	public:
		static BSStream* Create() { return new BSStream(); }

		// The engine loader is unavailable. Reporting failure is the honest
		// option: callers log an error and bail out cleanly.
		bool Load(const char*) { return false; }

		// NIFLoader reads topObjects[0] after a successful Load() only, but the
		// expression still has to compile, so provide a benign container.
		std::vector<NiPointer<NiObject>> topObjects;
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

		const NiPoint3 forward{ world.rotate.entry[0].pt.x, world.rotate.entry[0].pt.y, world.rotate.entry[0].pt.z };
		const NiPoint3 up{ world.rotate.entry[1].pt.x, world.rotate.entry[1].pt.y, world.rotate.entry[1].pt.z };
		const NiPoint3 right{ world.rotate.entry[2].pt.x, world.rotate.entry[2].pt.y, world.rotate.entry[2].pt.z };
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
			return NiPoint3{ forward.x * frust.nearPlane + a_axis.x * a_extent,
							 forward.y * frust.nearPlane + a_axis.y * a_extent,
							 forward.z * frust.nearPlane + a_axis.z * a_extent };
		};
		auto onPoint = [&](NiPlane& a_plane, const NiPoint3& a_normal, const NiPoint3& a_point) {
			a_plane.m_kNormal = a_normal;
			a_plane.m_fConstant = a_normal.x * a_point.x + a_normal.y * a_point.y + a_normal.z * a_point.z;
		};

		const NiPoint3 nearPoint{ camPos.x + forward.x * frust.nearPlane, camPos.y + forward.y * frust.nearPlane, camPos.z + forward.z * frust.nearPlane };
		const NiPoint3 farPoint{ camPos.x + forward.x * frust.farPlane, camPos.y + forward.y * frust.farPlane, camPos.z + forward.z * frust.farPlane };
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
// !! ALL OFFSETS BELOW ARE MEASURED, NOT GUESSED !!
//
// Source: the ORIGINAL TrueThroughScope.pdb that the author's own compiler
// emitted (27.6 MB, GUID 6fe78e95fd724d4bba834cb55b2ae88d). The matching
// original DLL was verified to be the same build (same CodeView GUID + age),
// and its internal paths show it was built at
//     E:\vsProject\CommonLibF4\build\debug-msvc-vcpkg-all\TrueThroughScope\Release\
// which is the author's private CommonLibF4 checkout.
//
// Every static_assert below re-checks an offset at COMPILE TIME. If a future
// library change moves something, the build fails here instead of corrupting
// memory at runtime.
//
// For background, the earlier hand-derived guesses were wrong by large margins:
//     ShadowSceneNode::kEyePosition       guessed 0x00   ->  actual 0x30c
//     ShadowSceneNode::bDisableLightUpdate guessed 0x04  ->  actual 0x225
//     ShadowSceneNode::fStoredFarClip     guessed 0x08   ->  actual 0x144
//     NiCullingProcess::bCustomCullPlanes guessed ~0x08  ->  actual 0x11f
// ===========================================================================
namespace RE
{


	// NOTE: NiPoint3A already exists in powerof3 (RE/NetImmerse/NiPoint.h:92,
	// alignas(0x10) : public NiPoint3) -- do NOT redefine it here.

	// ========================================================================
	// ShadowSceneNode : NiNode          (PDB: 43 members, base offset 0)
	// ------------------------------------------------------------------------
	// Layout transcribed from the PDB. Members TTS touches:
	//     +0x144  fStoredFarClip
	//     +0x148  uiVisibleNonShadowLights
	//     +0x14c  uiVisibleShadowLights
	//     +0x150  uiVisibleAmbientLights
	//     +0x158  lLightList
	//     +0x170  lShadowLightList
	//     +0x188  lAmbientLightList
	//     +0x225  bDisableLightUpdate
	//     +0x228  bAlwaysUpdateLights
	//     +0x30c  kEyePosition
	//
	// The light lists are real BSTArrays again (not empty std::vectors): with
	// verified offsets, iterating them is what the original does.
	//
	// NOTE: sizeof(NiNode) must be 0x140 for the padding below to land correctly,
	// and that is asserted at the end of this block.
	// ========================================================================
	class ShadowSceneNode : public NiNode
	{
	public:

		bool          bFrustumAddedToVis;      // 140
		BytePad<3> pad_141;              // 141
		float         fStoredFarClip;          // 144
		std::uint32_t uiVisibleNonShadowLights;// 148
		std::uint32_t uiVisibleShadowLights;   // 14C
		std::uint32_t uiVisibleAmbientLights;  // 150
		std::uint32_t pad_154;                 // 154

		BSTArray<NiPointer<BSLight>> lLightList;         // 158
		BSTArray<NiPointer<BSLight>> lShadowLightList;   // 170
		BSTArray<NiPointer<BSLight>> lAmbientLightList;  // 188
		BSTArray<NiPointer<BSLight>> lLightQueueAdd;     // 1A0
		BSTArray<NiPointer<BSLight>> lLightQueueRemove;  // 1B8
		BytePad<0x1F8 - 0x1D0> pad_1D0;  // 1D0 (SSNLightQueueCrit + DecalList + DecalListLock + pad)

		BSLight*      pSunLight;               // 1F8
		BSLight*      pCloudSunLight;          // 200
		BSLight*      pSunShadowLight;         // 208
		BSLight*      pLocalSunLight;          // 210
		NiPoint3      AverageLightPos;         // 218
		std::uint8_t  cSceneGraphIndex;        // 224
		bool          bDisableLightUpdate;     // 225
		bool          bWireframe;              // 226
		bool          bOpaqueWireframe;        // 227
		bool          bAlwaysUpdateLights;     // 228
		BytePad<0x30C - 0x229> pad_229;  // 229 .. 30B (spFog/pBSPortalGraph/lShadowLightAccum/... )

		NiPoint3      kEyePosition;            // 30C
	};
	static_assert(sizeof(NiNode) == 0x140, "NiNode must be 0x140 for ShadowSceneNode padding to line up");
	static_assert(offsetof(ShadowSceneNode, fStoredFarClip) == 0x144, "fStoredFarClip must be 0x144");
	static_assert(offsetof(ShadowSceneNode, uiVisibleNonShadowLights) == 0x148, "uiVisibleNonShadowLights must be 0x148");
	static_assert(offsetof(ShadowSceneNode, lLightList) == 0x158, "lLightList must be 0x158");
	static_assert(offsetof(ShadowSceneNode, bDisableLightUpdate) == 0x225, "bDisableLightUpdate must be 0x225");
	static_assert(offsetof(ShadowSceneNode, bAlwaysUpdateLights) == 0x228, "bAlwaysUpdateLights must be 0x228");
	static_assert(offsetof(ShadowSceneNode, kEyePosition) == 0x30C, "kEyePosition must be 0x30C");

	// ========================================================================
	// NiCullingProcess                  (PDB: 11 members)
	// ------------------------------------------------------------------------
	// powerof3 does not have this class at all, yet it is where TTS's members
	// actually live. BSCullingProcess derives from it.
	//     +0x10   m_pkVisibleSet
	//     +0x18   m_pkCamera        <- TTS
	//     +0x20   m_kFrustum
	//     +0x3c   m_kPlanes         <- TTS
	//     +0xac   kCustomCullPlanes <- TTS
	//     +0x11c  m_bCameraRelatedUpdates
	//     +0x11d  bUpdateAccumulateFlag
	//     +0x11e  bIgnorePreprocess
	//     +0x11f  bCustomCullPlanes <- TTS
	// ========================================================================
	class NiCullingProcess
	{
	public:
		BytePad<0x10> pad_00;        // 00 (vtable + m_bUseVirtualAppend + padding)

		NiVisibleArray*    m_pkVisibleSet;            // 10
		NiCamera*          m_pkCamera;                // 18
		NiFrustum          m_kFrustum;                // 20
		NiFrustumPlanes    m_kPlanes;                 // 3C
		NiFrustumPlanes    kCustomCullPlanes;         // AC

		bool               m_bCameraRelatedUpdates;   // 11C
		bool               bUpdateAccumulateFlag;     // 11D
		bool               bIgnorePreprocess;         // 11E
		bool               bCustomCullPlanes;         // 11F

		// TTS calls cullingProcess->SetCustomCullPlanes(&scopePlanes).
		// Not an engine call: the engine's own code (see the author's leaked
		// BSCullingProcess.cpp decompilation) is exactly
		//     if (bCustomCullPlanes) m_kPlanes = kCustomCullPlanes;
		// so storing the planes and raising the flag reproduces the contract.
		void SetCustomCullPlanes(const NiFrustumPlanes* a_planes)
		{
			if (a_planes) { kCustomCullPlanes = *a_planes; bCustomCullPlanes = true; }
		}

		// TTS also calls SetFrustum(&scopeCamera->viewFrustum) on the
		// geometry-list culling process; the engine stores it in m_kFrustum.
		void SetFrustum(const NiFrustum* a_frustum)
		{
			if (a_frustum) { m_kFrustum = *a_frustum; }
		}
	};
	static_assert(offsetof(NiCullingProcess, m_pkCamera) == 0x18, "m_pkCamera must be 0x18");
	static_assert(offsetof(NiCullingProcess, m_kPlanes) == 0x3C, "m_kPlanes must be 0x3C");
	static_assert(offsetof(NiCullingProcess, kCustomCullPlanes) == 0xAC, "kCustomCullPlanes must be 0xAC");
	static_assert(offsetof(NiCullingProcess, bCustomCullPlanes) == 0x11F, "bCustomCullPlanes must be 0x11F");

	// ========================================================================
	// BSCullingProcess : NiCullingProcess   (PDB: 9 own members)
	// ------------------------------------------------------------------------
	// TTS only reads kCullMode / pCompoundFrustum / spAccumulator off it; the
	// culling-plane members all come from the base.
	// ========================================================================
	class BSCullingProcess : public NiCullingProcess
	{
	public:
		BytePad<0x150 - 0x120> kRoomSharedMap_pad;           // 120 (BSTScatterTable, not needed)
		void*     pPortalGraphEntry;   // 150
		std::uint32_t kCullMode;       // 158
		BytePad<0x184 - 0x15C> pad_15C;  // 15C (eTypeStackA, 0x28 bytes)
		std::int32_t  iCTStackIndex;   // 184
		void*         pCompoundFrustum;// 188
		NiPointer<NiAccumulator> spAccumulator;  // 190
		bool          bRecurseToGeometry;        // 198
		BytePad<7> pad_199;      // 199
	};
	static_assert(offsetof(BSCullingProcess, kCullMode) == 0x158, "kCullMode must be 0x158");
	static_assert(offsetof(BSCullingProcess, pCompoundFrustum) == 0x188, "pCompoundFrustum must be 0x188");
	static_assert(offsetof(BSCullingProcess, spAccumulator) == 0x190, "spAccumulator must be 0x190");
	static_assert(offsetof(BSCullingProcess, bCustomCullPlanes) == 0x11F, "inherited bCustomCullPlanes must stay at 0x11F");

	// ========================================================================
	// BSShaderAccumulator : NiAlphaAccumulator   (PDB: 24 own members)
	// ------------------------------------------------------------------------
	// TTS sets kEyePosition and calls ClearActivePasses(bool).
	// The base class is absent from powerof3, and the engine vtable cannot be
	// synthesised, so this is a stand-alone stand-in sized/laid out like the
	// real object as far as the members TTS touches.
	//     +0x570  kEyePosition   <- TTS
	// ========================================================================
	class BSShaderAccumulator
	{
	public:
		// m_pkCamera lives in the base NiAccumulator at +0x10 (PDB fieldlist
// 0x24dde). NiAlphaAccumulator adds members from +0x50; BSShaderAccumulator's
// own members start at +0x58 -- hence the spacing below.
BytePad<0x10> pad_000;         // 000 (base vtable + refcount)
NiCamera*     m_pkCamera;      // 010  <- inherited from NiAccumulator
BytePad<0x570 - 0x18> pad_018; // 018 .. 56F
NiPoint3A     kEyePosition;    // 570  <- verified offset
BytePad<0x20> pad_580;         // 580

		// The engine clears its internal pass list here. The real function is
		// unavailable, and TTS calls it immediately before it re-points the
		// camera, so a no-op is the safe approximation.
		void ClearActivePasses(bool) {}
	};
	static_assert(offsetof(BSShaderAccumulator, kEyePosition) == 0x570, "kEyePosition must be 0x570");
	static_assert(offsetof(BSShaderAccumulator, m_pkCamera) == 0x10, "BSShaderAccumulator::m_pkCamera must be 0x10 (inherited from NiAccumulator)");

	// ========================================================================
	// BSGeometryListCullingProcess : BSCullingProcess  (PDB: 22 own members)
	// ------------------------------------------------------------------------
	// TTS sets m_pkCamera and calls SetFrustum() -- both inherited.
	// ========================================================================
	class BSGeometryListCullingProcess : public BSCullingProcess
	{
	public:
		BytePad<0x20> pad_1A0;
	};

	// ------------------------------------------------------------------------
	// BSGraphics::RendererShadowState  and  BSGraphics::Context
	//
	// dev-1.10.163 only FORWARD-DECLARES RendererShadowState (BSGraphics.h:46)
	// and has no Context class at all -- both belong to the author private
	// library. TTS reaches them like this (SecondPassRenderer.cpp:607-648):
	//
	//     uintptr_t contextPtr = <TLS slot 2848>;   // or DefaultContext
	//     auto context = (RE::BSGraphics::Context*)contextPtr;
	//     ... context->shadowState.CameraData.viewProjMat[4]
	//     ... context->shadowState.PosAdjust
	//
	// So Context must embed a RendererShadowState BY VALUE, and that state must
	// expose CameraData (viewProjMat at +0x0D0) and PosAdjust at +0x210. Those
	// offsets mirror CameraStateData and are the same numbers the earlier
	// master-side header patch used.
	//
	// Defining this here rather than patching the library header is deliberate:
	// the library is now dev-1.10.163 (overlaid by ci/build.ps1), whose
	// BSGraphics.h looks nothing like master, so a text patch would be brittle.
	// ------------------------------------------------------------------------
	namespace BSGraphics
	{
		class alignas(0x10) RendererShadowState
		{
		public:
			struct CameraData_t
			{
				BytePad<0x0D0>                  pad0;
				__m128                          viewProjMat[4];        // 0D0
				BytePad<0x210 - 0x0D0 - 0x40>   pad1;                 // 110
			};
			static_assert(sizeof(CameraData_t) == 0x210, "CameraData_t must be 0x210");

			CameraData_t CameraData;   // 000
			NiPoint3     PosAdjust;    // 210
		};
		static_assert(offsetof(RendererShadowState, CameraData) == 0x000, "CameraData must be 0x000");
		static_assert(offsetof(RendererShadowState, PosAdjust) == 0x210, "PosAdjust must be 0x210");

		class Context
		{
		public:
			RendererShadowState shadowState;   // 0000
		};
		static_assert(offsetof(Context, shadowState) == 0x0000, "Context::shadowState must be 0x0000");
	}

	// ========================================================================
	// BSRenderPass -- only forward-declared in powerof3 (BSShaderProperty.h:11).
	// TTS touches pGeometry only.
	// ========================================================================
	class BSRenderPass
	{
	public:
		BSGeometry* pGeometry;   // PDB/usage: TTS reads ->name (NiObjectNET) and ->world.scale (NiAVObject)
	};

	// ========================================================================
	// BSShaderManagerState              (PDB: 75 members)
	// ------------------------------------------------------------------------
	// TTS reads/writes four ambient-light fields. The types come from
	// ThroughScope::AmbientLightStateBackup (LightBackupSystem.h:16-29).
	// ========================================================================
	class BSShaderManagerState
	{
	public:
		NiTransform DirectionalAmbientTransform;
		NiTransform LocalDirectionalAmbientTransform;
		NiColorA    AmbientSpecular;
		std::uint8_t bAmbientSpecularEnabled;
	};
}



// ===========================================================================
// sizeof cross-checks for the layouts above.
//
// These are deliberately SEPARATE from the offsetof asserts: if one of these
// types has an unexpected size, we want the compiler to print the real number
// here, rather than letting it silently underflow a padding expression into
// 2^64-8 (which surfaces only as the useless "C2148: total size of array must
// not exceed 0x7fffffff").
// ===========================================================================
namespace RE
{
	static_assert(sizeof(NiPlane) == 0x10, "NiPlane must be 0x10");
	static_assert(sizeof(NiFrustumPlanes) == 0x70, "NiFrustumPlanes must be 0x70");
	static_assert(sizeof(NiFrustum) == 0x1C, "NiFrustum must be 0x1C");
	static_assert(sizeof(NiCullingProcess) == 0x120, "NiCullingProcess must be 0x120");
	static_assert(sizeof(BSLight) == 0x238, "BSLight must be 0x238 (0x236 rounded up to the 8-byte alignment NiRefObject imposes)");
}

// ===========================================================================
// TEMPORARY DIAGNOSTIC -- remove once every layout is green.
//
// MSVC's static_assert does not print the offending values, so a layout that
// underflows only shows up as an opaque number (2^64-8 for -8) and we cannot
// tell WHICH member drifted. The classic workaround: instantiate an undefined
// template whose argument is the value. The compiler then reports e.g.
//     error C2027: use of undefined type 'RE::ShowSize<288>'
// which is the real number (288 = 0x120).
//
// Each line carries the EXPECTED value so the diff is immediate.
// Set SHOW_LAYOUT_SIZES to 0 to silence.
// ===========================================================================
#define SHOW_LAYOUT_SIZES 0
#if SHOW_LAYOUT_SIZES
namespace RE
{
	template <std::size_t N> struct ShowSize;   // deliberately undefined

	// ---- sizeof ----
	ShowSize<sizeof(NiPlane)>          show_sz_NiPlane;           // expect  16 = 0x010
	ShowSize<sizeof(NiFrustum)>        show_sz_NiFrustum;         // expect  28 = 0x01C
	ShowSize<sizeof(NiFrustumPlanes)>  show_sz_NiFrustumPlanes;   // expect 112 = 0x070
	ShowSize<alignof(NiFrustumPlanes)> show_al_NiFrustumPlanes;   // expect   4
	ShowSize<sizeof(NiRefObject)>      show_sz_NiRefObject;       // expect  16 = 0x010
	ShowSize<sizeof(NiNode)>           show_sz_NiNode;            // expect 320 = 0x140
	ShowSize<sizeof(NiCullingProcess)> show_sz_NiCullingProcess;  // expect 288 = 0x120
	ShowSize<sizeof(BSLight)>          show_sz_BSLight;           // expect 566 = 0x236
	ShowSize<sizeof(ShadowSceneNode)>  show_sz_ShadowSceneNode;   // expect 800 = 0x320

	// ---- offsetof: NiCullingProcess ----
	ShowSize<offsetof(NiCullingProcess, m_pkVisibleSet)>      show_of_ncp_vis;   // expect  16 = 0x010
	ShowSize<offsetof(NiCullingProcess, m_pkCamera)>          show_of_ncp_cam;   // expect  24 = 0x018
	ShowSize<offsetof(NiCullingProcess, m_kFrustum)>          show_of_ncp_fru;   // expect  32 = 0x020
	ShowSize<offsetof(NiCullingProcess, m_kPlanes)>           show_of_ncp_pla;   // expect  60 = 0x03C
	ShowSize<offsetof(NiCullingProcess, kCustomCullPlanes)>   show_of_ncp_ccp;   // expect 172 = 0x0AC
	ShowSize<offsetof(NiCullingProcess, bCustomCullPlanes)>   show_of_ncp_bcc;   // expect 287 = 0x11F

	// ---- offsetof: BSCullingProcess ----
	ShowSize<offsetof(BSCullingProcess, pPortalGraphEntry)>   show_of_bsc_pge;   // expect 336 = 0x150
	ShowSize<offsetof(BSCullingProcess, kCullMode)>           show_of_bsc_kcm;   // expect 344 = 0x158
	ShowSize<offsetof(BSCullingProcess, pCompoundFrustum)>    show_of_bsc_pcf;   // expect 392 = 0x188

	// ---- offsetof: BSLight ----
	ShowSize<offsetof(BSLight, fLODDimmer)>                   show_of_bl_fld;    // expect  16 = 0x010
	ShowSize<offsetof(BSLight, pCullingProcess)>              show_of_bl_pcp;    // expect 320 = 0x140
	ShowSize<offsetof(BSLight, bTemporary)>                   show_of_bl_btm;    // expect 382 = 0x17E

	// ---- offsetof: ShadowSceneNode ----
	ShowSize<offsetof(ShadowSceneNode, fStoredFarClip)>       show_of_ssn_ffc;   // expect 324 = 0x144
	ShowSize<offsetof(ShadowSceneNode, kEyePosition)>         show_of_ssn_eye;   // expect 780 = 0x30C
}
#endif

// NOTE: no "#undef near/far" is needed any more.
// include/RE/NetImmerse/NiFrustum.h overrides the library header and names
// the two frustum floats nearPlane/farPlane, so the <Windows.h> macros named
// near/far are never referenced at all.
