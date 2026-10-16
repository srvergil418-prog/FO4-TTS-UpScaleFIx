
#include "Constants.h"
#include "D3DHooks.h"
#include "GlobalTypes.h"
#include "HookManager.h"
#include "RenderUtilities.h"
#include "ScopeCamera.h"
#include "Utilities.h"
#include <EventHandler.h>
#include <NiFLoader.h>
#include <Windows.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <format>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#include <winternl.h>

#include "DataPersistence.h"
#include "ImGuiManager.h"
#include "rendering/RenderStateManager.h"
#include "rendering/ScopeRenderingManager.h"
#include "rendering/RenderTargetMerger.h"
#include "rendering/ScopeCulling.h"
#include "ENBIntegration.h"
// ==================== TTS_TRACE_MARKER ====================
// Earliest-possible diagnostic trace. Depends on nothing but Win32/CRT --
// no spdlog, no F4SE, no TTS component -- so it survives to run even if
// static initialisation is what is killing us.
//
// Writes to several candidate paths because MO2's USVFS may intercept file
// creation, and uses BOTH the Win32 API and the CRT so that whichever path
// is hooked, the other may still get through.
#define TTS_TRACE_CANDIDATES \
    "E:\\Custom Fallout4\\tts-trace.log", \
    "E:\\tts-trace.log", \
    "D:\\dsh\\bld\\tts-trace.log", \
    "C:\\Users\\SRVERGIL\\tts-trace.log"

static void TTS_TraceWrite(const char* a_msg)
{
    static const char* kPaths[] = { TTS_TRACE_CANDIDATES };

    for (int i = 0; i < 4; ++i) {
        // 1) Win32
        HANDLE h = CreateFileA(kPaths[i], GENERIC_WRITE,
                               FILE_SHARE_READ | FILE_SHARE_WRITE,
                               NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
        if (h != INVALID_HANDLE_VALUE) {
            SetFilePointer(h, 0, NULL, FILE_END);
            DWORD w = 0;
            WriteFile(h, a_msg, (DWORD)strlen(a_msg), &w, NULL);
            WriteFile(h, "\r\n", 2, &w, NULL);
            CloseHandle(h);
            return;
        }
        // 2) CRT -- different API path, may bypass a Win32-only hook
#pragma warning(push)
#pragma warning(disable : 4996)   // fopen: SDL deprecation warning, deliberate here
        FILE* f = fopen(kPaths[i], "ab");
#pragma warning(pop)
        if (f) {
            fwrite(a_msg, 1, strlen(a_msg), f);
            fwrite("\r\n", 1, 2, f);
            fclose(f);
            return;
        }
    }
}

#define TTS_TRACE(msg) TTS_TraceWrite(msg)

extern "C" BOOL WINAPI DllMain(HINSTANCE, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH) {
        TTS_TraceWrite("DllMain PROCESS_ATTACH");
    }
    return TRUE;
}
// ================== end TTS_TRACE_MARKER ==================
using namespace RE;
using namespace RE::BSGraphics;

#pragma region Pointer
// RELGlobalTypes.h
// ptr_DrawWorld1stPerson  ThroughScope 
REL::Relocation<bool*> ptr_DrawWorld_b1stPersonEnable{ REL::ID(922366) };
REL::Relocation<bool*> ptr_DrawWorld_b1stPersonInWorld{ REL::ID(34473) };
REL::Relocation<BSCullingGroup**> ptr_k1stPersonCullingGroup{ REL::ID(731482) };
static REL::Relocation<Context**> ptr_DefaultContext{ REL::ID(33539) };
REL::Relocation<uint32_t*> ptr_tls_index{ REL::ID(842564) };

REL::Relocation<ZPrePassDrawData**> ptr_pFPZPrePassDrawDataA{ REL::ID(548629) };
REL::Relocation<ZPrePassDrawData**> ptr_pZPrePassDrawDataA{ REL::ID(1503321) };

REL::Relocation<AlphaTestZPrePassDrawData**> ptr_pFPAlphaTestZPrePassDrawDataA{ REL::ID(919131) };
REL::Relocation<AlphaTestZPrePassDrawData**> ptr_pAlphaTestZPrePassDrawDataA{ REL::ID(297801) };

static REL::Relocation<uint32_t*> ZPrePassDrawDataCount{ REL::ID(844802) };
static REL::Relocation<uint32_t*> MergeInstancedZPrePassDrawDataCount{ REL::ID(1283533) };
static REL::Relocation<uint32_t*> AlphaTestZPrePassDrawDataCount{ REL::ID(1064092) };
static REL::Relocation<uint32_t*> AlphaTestMergeInstancedZPrePassDrawDataCount{ REL::ID(602241) };

static REL::Relocation<BSShaderManagerState**> BSM_ST{ REL::ID(1327069) };

// LightStateBackupGlobalTypes.h
std::vector<ThroughScope::LightStateBackup> ThroughScope::g_LightStateBackups;

#pragma endregion

// GlobalTypes.h
uint64_t ThroughScope::savedDrawWorld = 0;
RE::PlayerCharacter* ThroughScope::g_pchar = nullptr;
// isScopCamReady  isRenderReady  RenderStateManager
ThroughScope::D3DHooks* ThroughScope::d3dHooks = nullptr;
NIFLoader* ThroughScope::nifloader = nullptr;
HMODULE ThroughScope::upscalerModular = nullptr;
RE::NiCamera* ThroughScope::ggg_ScopeCamera = nullptr;

//  ViewProjMat SetCameraData 
__m128 ThroughScope::g_ScopeViewProjMat[4] = {};
bool ThroughScope::g_ScopeViewProjMatValid = false;

//  Scope ViewProjMat Motion Vector 
__m128 ThroughScope::g_ScopePreviousViewProjMat[4] = {};
bool ThroughScope::g_ScopePreviousViewProjMatValid = false;

//  FOV PlayerCamera->firstPersonFOV 
float ThroughScope::g_MainCameraFOV = 70.0f;  // 

// RELGlobalTypes.h
REL::Relocation<RE::ShadowSceneNode**> ThroughScope::ptr_DrawWorldShadowNode{ REL::ID(1327069) };
REL::Relocation<RE::NiCamera**> ThroughScope::ptr_DrawWorldCamera{ REL::ID(1444212) };
REL::Relocation<RE::NiCamera**> ThroughScope::ptr_DrawWorld1stCamera{ REL::ID(380177) };
REL::Relocation<RE::NiCamera**> ThroughScope::ptr_DrawWorldVisCamera{ REL::ID(81406) };
REL::Relocation<RE::NiCamera**> ThroughScope::ptr_BSShaderManagerSpCamera{ REL::ID(543218) };
REL::Relocation<RE::NiCamera**> ThroughScope::ptr_DrawWorldSpCamera{ REL::ID(543218) };
REL::Relocation<RE::BSShaderAccumulator**> ThroughScope::ptr_DrawWorldAccum{ REL::ID(1211381) };
REL::Relocation<RE::BSShaderAccumulator**> ThroughScope::ptr_Draw1stPersonAccum{ REL::ID(1430301) };
REL::Relocation<RE::BSGeometryListCullingProcess**> ThroughScope::DrawWorldGeomListCullProc0{ REL::ID(865470) };
REL::Relocation<RE::BSGeometryListCullingProcess**> ThroughScope::DrawWorldGeomListCullProc1{ REL::ID(1084947) };
REL::Relocation<RE::BSCullingProcess**> ThroughScope::DrawWorldCullingProcess{ REL::ID(520184) };
REL::Relocation<uint32_t*> ThroughScope::FPZPrePassDrawDataCount{ REL::ID(163482) };
REL::Relocation<uint32_t*> ThroughScope::FPAlphaTestZPrePassDrawDataCount{ REL::ID(382658) };
REL::Relocation<RE::NiAVObject**> ThroughScope::ptr_DrawWorld1stPerson{ REL::ID(1491228) };

// 
static ThroughScope::RenderStateManager* g_renderStateMgr = ThroughScope::RenderStateManager::GetSingleton();

static ThroughScope::RendererShadowState* GetRendererShadowState()
{
	_TEB* teb = NtCurrentTeb();
	Context* context;

	auto tls_index = *ptr_tls_index;
	context = *(Context**)(*((uint64_t*)teb->Reserved1[11] + tls_index) + 2848i64);

	if (!context) {
		auto defaultContext = *ptr_DefaultContext;
		context = defaultContext;
	}

	return &context->shadowState;
}

using namespace ThroughScope;
using namespace ThroughScope::Utilities;

static HookManager* g_hookMgr = HookManager::GetSingleton();

namespace ThroughScope
{

	using namespace ::ThroughScope::Utilities;

	//renderTargets[0] = SwapChainImage RenderTarget(Only rtView and srView)
	//renderTargets[4] = Main Render_PreUI RenderTarget
	//renderTargets[26] = TAA  = TAA PS t1
	//renderTargets[29] = TAA Motion Vectors = TAA PS t2
	//renderTargets[24] = TAA Jitter Mask = TAA PS t4 
	//renderTargets[15] =  1920 -> 480 
	//renderTargets[69] = 1x1 

	// RenderStateManager
	namespace FirstSpawnDelay
	{
		bool delayStarted = false;
		std::chrono::steady_clock::time_point delayStartTime;

		void Reset()
		{
			delayStarted = false;
			// delayStartTime 
		}
	}

	void ResetFirstSpawnState()
	{
		ScopeCamera::hasFirstSpawnNode = false;
		ScopeCamera::isDelayStarted = false;
		ScopeCamera::isFirstScopeRender = true;
		D3DHooks::ResetParallaxState();

		// ZoomData
		std::thread([]() {
			std::this_thread::sleep_for(std::chrono::milliseconds(1000));  // 
			ScopeCamera::RestoreZoomDataForCurrentWeapon();
			logger::info("Restored ZoomData after load game");
		}).detach();
	}
} // namespace ThroughScope




// Initialization thread function
DWORD WINAPI InitThread(HMODULE hModule)
{
	while (!BSGraphics::RendererData::GetSingleton() 
		|| !BSGraphics::RendererData::GetSingleton()->renderWindow 
		|| !BSGraphics::RendererData::GetSingleton()->renderWindow->hwnd 
		|| !BSGraphics::RendererData::GetSingleton()->renderWindow->swapChain 
		|| !RE::BSGraphics::RendererData::GetSingleton()->device 
		|| !RE::BSGraphics::RendererData::GetSingleton()->context) {
		Sleep(10);
	}

	// Wait for the game world to be fully loaded
	while (!RE::PlayerCharacter::GetSingleton() || !RE::PlayerCharacter::GetSingleton()->Get3D() || !RE::PlayerControls::GetSingleton() || !RE::PlayerCamera::GetSingleton() || !RE::Main::WorldRootCamera()) 
	{
		Sleep(500);
	}
	logger::info("TrueThroughScope: Game world loaded, initializing ThroughScope...");

	// Initialize systems
	logger::info("TrueThroughScope: About to initialize ImGui - checking for AntTweakBar conflicts...");
	g_renderStateMgr->SetImGuiManagerInit(ThroughScope::ImGuiManager::GetSingleton()->Initialize());

	bool scopeReady = ThroughScope::ScopeCamera::Initialize();
	bool renderReady = ThroughScope::RenderUtilities::Initialize();
	
	// Initialize RenderTargetMerger for centralized RT backup/merge
	if (renderReady) {
		ThroughScope::RenderTargetMerger::GetInstance().Initialize();
	}

	// Update render state manager
	g_renderStateMgr->SetScopeReady(scopeReady);
	g_renderStateMgr->SetRenderReady(renderReady);
	ThroughScope::ggg_ScopeCamera = ThroughScope::ScopeCamera::GetScopeCamera();
	
	//  DataPersistence 
	auto dataPersistence = ThroughScope::DataPersistence::GetSingleton();
	const auto& globalSettings = dataPersistence->GetGlobalSettings();
	ThroughScope::SetCullingSafetyMargin(globalSettings.cullingSafetyMargin);
	ThroughScope::SetShadowCasterRange(globalSettings.shadowCasterRange);

	logger::info("TrueThroughScope: ThroughScope initialization completed");
	return 0;
}

// Initialize the plugin
void InitializePlugin()
{
	logger::info("TrueThroughScope: Plugin initialization started");

	ThroughScope::g_pchar = RE::PlayerCharacter::GetSingleton();
	g_hookMgr->RegisterAllHooks();
	ThroughScope::EquipWatcher::GetSingleton()->Initialize();
	ThroughScope::AnimationGraphEventWatcher::GetSingleton()->Initialize();

	// Start initialization thread for components that need the game world
	HANDLE hThread = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)InitThread, (HMODULE)REX::W32::GetCurrentModule(), 0, NULL);

	// Initialize RenderStateManager with default values
	g_renderStateMgr->Initialize();
	
	// Initialize ScopeRenderingManager for fo4test compatibility detection
	ThroughScope::ScopeRenderingManager::GetSingleton()->Initialize();
	
	// Initialize ENB integration (will detect ENB and register callback if present)
	ThroughScope::ENBIntegration::GetSingleton()->Initialize();
}

// F4SE Query plugin
F4SE_EXPORT bool F4SEAPI F4SEPlugin_Query(const F4SE::QueryInterface* a_f4se, F4SE::PluginInfo* a_info)
{
	TTS_TRACE("F4SEPlugin_Query ENTER");
	a_info->infoVersion = F4SE::PluginInfo::kVersion;
	a_info->name = Version::PROJECT.data();
	a_info->version = Version::MAJOR;

	// Setup logging
#ifndef NDEBUG
	auto sink = std::make_shared<spdlog::sinks::msvc_sink_mt>();
#else
	auto path = logger::log_directory();
	if (!path) {
		return false;
	}

	*path /= fmt::format(FMT_STRING("{}.log"), Version::PROJECT);
	auto sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(path->string(), true);
#endif

	auto log = std::make_shared<spdlog::logger>("global log"s, std::move(sink));

#ifndef NDEBUG
	log->set_level(spdlog::level::trace);
#else
	log->set_level(spdlog::level::info);
	log->flush_on(spdlog::level::warn);
#endif

	spdlog::set_default_logger(std::move(log));
	spdlog::set_pattern("%g(%#): [%^%l%$] %v"s);

	logger::info(FMT_STRING("=== {} v{}.{}.{} ==="), Version::PROJECT, Version::MAJOR, Version::MINOR, Version::PATCH);

	if (a_f4se->IsEditor()) {
		logger::critical("loaded in editor");
		return false;
	}

	const auto ver = a_f4se->RuntimeVersion();
	if ((REL::Module::IsF4() && ver < F4SE::RUNTIME_1_10_163) ||
		(REL::Module::IsVR() && ver < F4SE::RUNTIME_LATEST_VR)) {
		logger::critical("unsupported runtime v{}", ver.string());
		return false;
	}

	logger::info("TrueThroughScope: Query successful");

	F4SE::AllocTrampoline(32 * 8);
	return true;
}

// F4SE Load plugin
F4SE_EXPORT bool F4SEAPI F4SEPlugin_Load(const F4SE::LoadInterface* a_f4se)
{
	TTS_TRACE("F4SEPlugin_Load ENTER");
	//SetConsoleOutputCP(CP_UTF8);
	//SetConsoleCP(CP_UTF8);

#ifdef _DEBUG
	while (!IsDebuggerPresent()) {
		Sleep(1000);
	}
	Sleep(1000);

#endif

	TTS_TRACE("LOAD-TRACE-01 before MH_Initialize");
	auto mhInit = MH_Initialize();
	TTS_TRACE("LOAD-TRACE-02 after MH_Initialize");
	// MinHook
	if (mhInit != MH_OK) {
		logger::info("MH_Initialize Not Ok, Reason: {}", (int)mhInit);
	}

	TTS_TRACE("LOAD-TRACE-03 before D3DHooks::GetSingleton");
	ThroughScope::d3dHooks = D3DHooks::GetSingleton();
	TTS_TRACE("LOAD-TRACE-04 after D3DHooks::GetSingleton");
	TTS_TRACE("LOAD-TRACE-05 before NIFLoader::GetSingleton");
	ThroughScope::nifloader = NIFLoader::GetSingleton();
	TTS_TRACE("LOAD-TRACE-06 after NIFLoader::GetSingleton");

	logger::info("TrueThroughScope: Ninja!");
	logger::info("TrueThroughScope: TrueThroughScope does NOT use AntTweakBar - any such errors are from other mods");

	TTS_TRACE("LOAD-TRACE-07 before PreInit");
	ThroughScope::d3dHooks->PreInit();
	TTS_TRACE("LOAD-TRACE-08 after PreInit");

		// VER-TRACE: Module::get().version() drives the address-library filename.
	// If it is not 1.10.163.0 the .bin lookup fails and IDDB report_and_fail()s.
	// EXC-TRACE: catch access violations too. catch(...) does NOT catch SEH,
	// and the crash inside F4SE::Init (-> REL::IDDB::get()) is a real
	// access violation, so use __try/__except and record the code + the
	// failing address.
	__try {
		F4SE::Init(a_f4se);
		TTS_TRACE("EXC-TRACE: F4SE::Init returned normally");
	} __except (EXCEPTION_EXECUTE_HANDLER) {
		char _ebuf[256] = { 0 };
		wsprintfA(_ebuf, "EXC-TRACE: SEH caught, code=0x%08lX", (unsigned long)GetExceptionCode());
		TTS_TraceWrite(_ebuf);
		return false;
	}
	TTS_TRACE("LOAD-TRACE-10 after F4SE::Init");
	// Register plugin for F4SE messages
	const F4SE::MessagingInterface* message = F4SE::GetMessagingInterface();
	if (!message) {
		logger::critical("Failed to get messaging interface");
		return false;
	}

	// Register for F4SE messages
	message->RegisterListener([](F4SE::MessagingInterface::Message* msg) -> void {
		if (msg->type == F4SE::MessagingInterface::kGameDataReady) {
			// Game data is ready - this is when we should initialize
			ThroughScope::upscalerModular = LoadLibraryA("Data/F4SE/Plugins/Fallout4Upscaler.dll");
			logger::info("TrueThroughScope: Game data ready, initializing plugin");
			ThroughScope::d3dHooks->Initialize();
			InitializePlugin();

			// 
			// ThroughScope::SetLaserInvestigationMode(true);
		} else if (msg->type == F4SE::MessagingInterface::kPostLoadGame) {
			logger::info("TrueThroughScope: Load a save, reset scope status");
			ThroughScope::ImGuiManager::GetSingleton()->ForceHideCursor();
			ResetFirstSpawnState();
		}
		else if (msg->type == F4SE::MessagingInterface::kNewGame)
		{
			ResetFirstSpawnState();
		}
	});

	return true;
}

// Version data for F4SE
F4SE_EXPORT constinit auto F4SEPlugin_Version = []() noexcept {
	F4SE::PluginVersionData data{};

	data.AuthorName(Version::AUTHOR);
	data.PluginName(Version::PROJECT);
	data.PluginVersion(Version::VERSION);

	data.UsesAddressLibrary(true);
	data.IsLayoutDependent(true);
	data.UsesSigScanning(false);
	data.HasNoStructUse(false);

	data.CompatibleVersions({ F4SE::RUNTIME_1_10_163 });
	return data;
}();
