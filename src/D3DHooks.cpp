#include "D3DHooks.h"
#include "Utilities.h"
#include "GlobalTypes.h"
#include <detours.h>
#include <imgui.h>
#include <imgui_impl_dx11.h>
#include <imgui_impl_win32.h>
#include <ScopeCamera.h>
#include <wrl/client.h>
#include <d3dcompiler.h>
#include "RenderUtilities.h"
#include "ScopeRenderingManager.h"

#include <DDSTextureLoader11.h>
#include "ImGuiManager.h"

// 使用 XInput 读取手柄按钮状态
// 注意：XInput 原生只支持 Xbox 控制器，但 Steam Input 会自动将 PS 控制器映射为 XInput
#include <xinput.h>
#pragma comment(lib, "xinput.lib")
#pragma comment(lib, "d3dcompiler.lib")


#include <renderdoc_app.h>
RENDERDOC_API_1_6_0* rdoc_api = nullptr;

static constexpr UINT MAX_VIEWPORTS = D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE;

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
typedef HRESULT (*D3D11Create)(
	_In_opt_ IDXGIAdapter* pAdapter,
	D3D_DRIVER_TYPE DriverType,
	HMODULE Software,
	UINT Flags,
	_In_reads_opt_(FeatureLevels) CONST D3D_FEATURE_LEVEL* pFeatureLevels,
	UINT FeatureLevels,
	UINT SDKVersion,
	_In_opt_ CONST DXGI_SWAP_CHAIN_DESC* pSwapChainDesc,
	_COM_Outptr_opt_ IDXGISwapChain** ppSwapChain,
	_COM_Outptr_opt_ ID3D11Device** ppDevice,
	_Out_opt_ D3D_FEATURE_LEVEL* pFeatureLevel,
	_COM_Outptr_opt_ ID3D11DeviceContext** ppImmediateContext);

D3D11Create m_D3D11CreateDeviceAndSwapChain_O;

namespace ThroughScope {
    
	using namespace Microsoft::WRL;
	ComPtr<IDXGISwapChain> D3DHooks::s_SwapChain = nullptr;
	WNDPROC D3DHooks::s_OriginalWndProc = nullptr;
	HRESULT(WINAPI* D3DHooks::s_OriginalPresent)(IDXGISwapChain*, UINT, UINT) = nullptr;
	RECT D3DHooks::oldRect{};
	IAStateCache D3DHooks::s_CachedIAState;
	VSStateCacheWithCopy D3DHooks::s_CachedVSState;
	RSStateCache D3DHooks::s_CachedRSState;
	OMStateCache D3DHooks::s_CachedOMState;
	float D3DHooks::s_ReticleScale = 1.0f;
	float D3DHooks::s_ReticleOffsetX = 0.5f;
	float D3DHooks::s_ReticleOffsetY = 0.5f;
	bool D3DHooks::s_ScaleReticleWithZoom = false;
	bool D3DHooks::s_HasCachedState = false;
	D3DHooks::CachedScopeConstantBuffer D3DHooks::s_CachedConstantBufferData; // 初始化缓存数据

	bool D3DHooks::s_isForwardStage = false;
	bool D3DHooks::s_EnableFOVAdjustment = true;
	float D3DHooks::s_FOVAdjustmentSensitivity = 1.0f;
	DWORD64 D3DHooks::s_LastGamepadInputTime = 0;

	// 为瞄准镜创建和管理资源的静态变量
	// Resources allocated in D3DResourceManager
	
	// ScopeQuad draw parameters for stencil write
	static UINT s_ScopeQuadIndexCount = 0;
	static UINT s_ScopeQuadStartIndexLocation = 0;
	static INT s_ScopeQuadBaseVertexLocation = 0;
	
	// ScopeQuad viewport capture for Upscaling compatibility
	// 保存检测到 scope quad 时的 viewport，用于后续渲染到 RT4 时正确变换坐标
	static D3D11_VIEWPORT s_ScopeQuadOriginalViewport = {};
	static bool s_HasScopeQuadViewport = false;

	bool D3DHooks::s_EnableRender = false;
	bool D3DHooks::s_InPresent = false;


	constexpr UINT MAX_SRV_SLOTS = 128;
	constexpr UINT MAX_SAMPLER_SLOTS = 16;
	constexpr UINT MAX_CB_SLOTS = 14; 

	// 新的视差参数默认值
	float D3DHooks::s_ParallaxStrength = 0.05f;        // 视差偏移强度
	float D3DHooks::s_ParallaxSmoothing = 0.5f;        // 时域平滑
	float D3DHooks::s_ExitPupilRadius = 0.45f;         // 出瞳半径
	float D3DHooks::s_ExitPupilSoftness = 0.15f;       // 出瞳边缘柔和度
	float D3DHooks::s_VignetteStrength = 0.3f;         // 晕影强度
	float D3DHooks::s_VignetteRadius = 0.7f;           // 晕影起始半径
	float D3DHooks::s_VignetteSoftness = 0.3f;         // 晕影柔和度
	float D3DHooks::s_EyeReliefDistance = 0.5f;        // 眼距
	int   D3DHooks::s_EnableParallax = 1;              // 启用视差

	// 高级视差参数
	float D3DHooks::s_ParallaxFogRadius = 1.0f;            // 边缘渐变半径
	float D3DHooks::s_ParallaxMaxTravel = 1.5f;            // 最大移动距离
	float D3DHooks::s_ReticleParallaxStrength = 0.5f;      // 准星偏移强度

    bool D3DHooks::s_IsCapturingHDR = false; // 定义静态变量
    uint64_t D3DHooks::s_FrameNumber = 0;  // 帧计数器

	// Parallax accumulation state
	bool D3DHooks::s_ParallaxInitialized = false;
	RE::NiPoint3 D3DHooks::s_AccumulatedParallaxOffset(0, 0, 0);

	void D3DHooks::ResetParallaxState()
	{
		s_ParallaxInitialized = false;
		s_AccumulatedParallaxOffset = RE::NiPoint3(0, 0, 0);
	}

	// 夜视效果参数初始化
	float D3DHooks::s_NightVisionIntensity = 1.0f;
	float D3DHooks::s_NightVisionNoiseScale = 0.05f;
	float D3DHooks::s_NightVisionNoiseAmount = 0.05f;
	float D3DHooks::s_NightVisionGreenTint = 1.2f;
	int D3DHooks::s_EnableNightVision = 0;



	// 球形畸变效果参数初始化
	float D3DHooks::s_SphericalDistortionStrength = 0.0f;
	float D3DHooks::s_SphericalDistortionRadius = 0.8f;
	float D3DHooks::s_SphericalDistortionCenterX = 0.0f;
	float D3DHooks::s_SphericalDistortionCenterY = 0.0f;
	int D3DHooks::s_EnableSphericalDistortion = 0;
	int D3DHooks::s_EnableChromaticAberration = 0;

	static constexpr UINT TARGET_STRIDE = 28;
	static constexpr UINT TARGET_INDEX_COUNT = 96;
	static constexpr UINT TARGET_BUFFER_SIZE = 0x0000000008000000;
	
	// TTS Marker UV - 用于可靠检测 TTSEffectShape 的唯一标识
	// 在 NIF 编辑时，将第一个顶点的 UV 设置为这个特殊值
	// UV 使用 half-float (16-bit) 格式存储在顶点缓冲区中
	static constexpr float TTS_MARKER_UV_U = 0.415411f;
	static constexpr float TTS_MARKER_UV_V = 0.189191f;
	static constexpr float TTS_MARKER_UV_TOLERANCE = 0.001f;
	typedef void(__stdcall* D3D11DrawIndexedHook)(ID3D11DeviceContext* pContext, UINT IndexCount, UINT StartIndexLocation, INT BaseVertexLocation);
	typedef void(__stdcall* D3D11RSSetViewportsHook)(ID3D11DeviceContext* pContext, UINT NumViewports, const D3D11_VIEWPORT* pViewports);
	using ClipCur = decltype(&ClipCursor);

	D3D11DrawIndexedHook phookD3D11DrawIndexed = nullptr;
	ClipCur phookClipCursor = nullptr;
	D3D11RSSetViewportsHook phookD3D11RSSetViewports = nullptr;

	D3DHooks* D3DInstance = D3DHooks::GetSingleton();
	ImGuiManager* imguiMgr;
	ID3D11DeviceContext* m_Context = nullptr;
	ID3D11Device* m_Device = nullptr;
	IDXGISwapChain* m_SwapChain = nullptr;

	bool D3DHooks::isSelfDrawCall = false;

	void D3DHooks::UpdateScopeParallaxSettings(float parallaxStrength, float exitPupilRadius, float vignetteStrength, float vignetteRadius)
	{
		s_ParallaxStrength = parallaxStrength;
		s_ExitPupilRadius = exitPupilRadius;
		s_VignetteStrength = vignetteStrength;
		s_VignetteRadius = vignetteRadius;

		// 强制下次更新常量缓冲区
		s_CachedConstantBufferData.screenWidth = -1.0f;

		//logger::info("Updated D3D scope settings - Parallax: {:.3f}, ExitPupil: {:.3f}, Vignette: {:.3f}/{:.3f}",
		//	parallaxStrength, exitPupilRadius, vignetteStrength, vignetteRadius);
	}

	void D3DHooks::UpdateParallaxAdvancedSettings(float smoothing, float exitPupilSoftness, float vignetteSoftness, float eyeRelief, int enableParallax)
	{
		s_ParallaxSmoothing = smoothing;
		s_ExitPupilSoftness = exitPupilSoftness;
		s_VignetteSoftness = vignetteSoftness;
		s_EyeReliefDistance = eyeRelief;
		s_EnableParallax = enableParallax;

		// 强制下次更新常量缓冲区
		s_CachedConstantBufferData.screenWidth = -1.0f;
	}

	void D3DHooks::UpdateNightVisionSettings(float intensity, float noiseScale, float noiseAmount, float greenTint)
	{
		s_NightVisionIntensity = intensity;
		s_NightVisionNoiseScale = noiseScale;
		s_NightVisionNoiseAmount = noiseAmount;
		s_NightVisionGreenTint = greenTint;
		
		// 强制下次更新
		s_CachedConstantBufferData.screenWidth = -1.0f;
	}



	void D3DHooks::UpdateSphericalDistortionSettings(float strength, float radius, float centerX, float centerY)
	{
		s_SphericalDistortionStrength = strength;
		s_SphericalDistortionRadius = std::clamp(radius, 0.0f, 1.0f);
		s_SphericalDistortionCenterX = std::clamp(centerX, -0.5f, 0.5f);
		s_SphericalDistortionCenterY = std::clamp(centerY, -0.5f, 0.5f);
		
		// 立即标记缓存为无效，强制下次更新
		s_CachedConstantBufferData.sphericalDistortionStrength = -999.0f; // 设置一个不可能的值
	}

	void D3DHooks::ForceConstantBufferUpdate()
	{
		// 通过修改缓存的screenWidth来强制更新
		s_CachedConstantBufferData.screenWidth = -1.0f;
	}


	D3DHooks* D3DHooks::GetSingleton()
	{
		static D3DHooks instance;
		return &instance;
	}

	ID3D11DeviceContext* D3DHooks::GetContext() 
	{
		return m_Context;
	}

	ID3D11Device* D3DHooks::GetDevice()
	{
		return m_Device;
	}

	HRESULT WINAPI D3DHooks::D3D11CreateDeviceAndSwapChain_Hook(
		_In_opt_ IDXGIAdapter* pAdapter,
		D3D_DRIVER_TYPE DriverType,
		HMODULE Software,
		UINT Flags,
		_In_reads_opt_(FeatureLevels) CONST D3D_FEATURE_LEVEL* pFeatureLevels,
		UINT FeatureLevels,
		UINT SDKVersion,
		_In_opt_ CONST DXGI_SWAP_CHAIN_DESC* pSwapChainDesc,
		_COM_Outptr_opt_ IDXGISwapChain** ppSwapChain,
		_COM_Outptr_opt_ ID3D11Device** ppDevice,
		_Out_opt_ D3D_FEATURE_LEVEL* pFeatureLevel,
		_COM_Outptr_opt_ ID3D11DeviceContext** ppImmediateContext)
	{
		auto hr = m_D3D11CreateDeviceAndSwapChain_O(
			pAdapter, DriverType, Software, Flags, pFeatureLevels, FeatureLevels,
			SDKVersion, pSwapChainDesc, ppSwapChain, ppDevice, pFeatureLevel, ppImmediateContext);

		m_SwapChain = *ppSwapChain;
		m_Device = *ppDevice;
		m_Device->GetImmediateContext(&m_Context);

		GetSingleton()->HookAllContexts();
		
		// Initialize D3DResourceManager
		if (!D3DResourceManager::GetSingleton()->Initialize(m_Device)) {
			logger::error("Failed to initialize D3DResourceManager");
		} else {
			logger::info("D3DResourceManager initialized successfully");
		}

		logger::info("D3D hooks initialized");

		return hr;
	}

	std::vector<ID3D11DeviceContext*> allContexts;

	void D3DHooks::HookContext(ID3D11DeviceContext* context)
	{
		// 获取正确的虚函数索引
		static UINT drawIndexedIndex = GetDrawIndexedIndex(context);

		void** vtable = *reinterpret_cast<void***>(context);
		void* drawIndexedFunc = vtable[drawIndexedIndex];

		// 避免重复hook
		static std::unordered_set<void*> hookedVtables;
		if (hookedVtables.find(vtable) != hookedVtables.end())
			return;

		hookedVtables.insert(vtable);

		Utilities::CreateAndEnableHook(drawIndexedFunc, reinterpret_cast<void*>(hkDrawIndexed), reinterpret_cast<LPVOID*>(&phookD3D11DrawIndexed), "DrawIndexedHook");
	}

	// 在设备创建后调用
	void D3DHooks::HookAllContexts()
	{
		// Hook主上下文
		HookContext(m_Context);


	}

	
	UINT D3DHooks::GetDrawIndexedIndex(ID3D11DeviceContext* context)
	{
		// 创建测试资源
		D3D11_BUFFER_DESC desc = { 0 };
		desc.ByteWidth = 1024;
		desc.BindFlags = D3D11_BIND_INDEX_BUFFER;

		ID3D11Buffer* testBuffer = nullptr;
		m_Device->CreateBuffer(&desc, nullptr, &testBuffer);

		// 设置测试状态
		context->IASetIndexBuffer(testBuffer, DXGI_FORMAT_R16_UINT, 0);

		// 特征码扫描
		const BYTE drawCallPattern[] = { 0x48, 0x8B, 0x01, 0xFF, 0x90 };  // mov rax,[rcx]; call [rax+...]

		for (UINT i = 0; i < 100; i++) {
			void* func = reinterpret_cast<void**>(*reinterpret_cast<void***>(context))[i];

			if (memcmp(func, drawCallPattern, sizeof(drawCallPattern)) == 0) {
				logger::info("Found DrawIndexed at index {}", i);
				testBuffer->Release();
				return i;
			}
		}

		testBuffer->Release();
		return 12;  // 默认值
	}

	bool isRenderDocDll = false;

	void InitRenderDoc()
	{
		HMODULE mod = LoadLibraryA("renderdoc.dll");
		if (!mod) {
			logger::error("Failed to load renderdoc.dll. Error code: {}", GetLastError());
			return;
		}

		pRENDERDOC_GetAPI RENDERDOC_GetAPI =
			(pRENDERDOC_GetAPI)GetProcAddress(mod, "RENDERDOC_GetAPI");
		if (!RENDERDOC_GetAPI) {
			logger::error("Failed to get RENDERDOC_GetAPI function. Error code: {}", GetLastError());
			return;
		}

		int ret = RENDERDOC_GetAPI(eRENDERDOC_API_Version_1_6_0, (void**)&rdoc_api);
		if (ret != 1) {
			logger::error("RENDERDOC_GetAPI failed with return code: {}", ret);
			return;
		}
		int major, minor, patch;
		rdoc_api->GetAPIVersion(&major, &minor, &patch);
		logger::info("RenderDoc API v{}.{}.{} loaded", major, minor, patch);
		// Set RenderDoc options
		rdoc_api->SetCaptureOptionU32(eRENDERDOC_Option_AllowFullscreen, 1);
		rdoc_api->SetCaptureOptionU32(eRENDERDOC_Option_AllowVSync, 1);
		rdoc_api->SetCaptureOptionU32(eRENDERDOC_Option_APIValidation, 1);
		rdoc_api->SetCaptureOptionU32(eRENDERDOC_Option_CaptureAllCmdLists, 1);
		rdoc_api->SetCaptureOptionU32(eRENDERDOC_Option_CaptureCallstacks, 1);
		rdoc_api->SetCaptureOptionU32(eRENDERDOC_Option_RefAllResources, 1);

		logger::info("RenderDoc initialized successfully");
	}


	bool D3DHooks::PreInit()
	{
#ifdef _DEBUG
		HMODULE mod = LoadLibraryA("renderdoc.dll");
		isRenderDocDll = mod;
		if (mod) {
			logger::info("Found RenderDoc.dll, Using Another Hook.");
			InitRenderDoc();
		}
#endif
		logger::info("D3D11 hooks loading...");
		ThroughScope::upscalerModular = LoadLibraryA("Data/F4SE/Plugins/Fallout4Upscaler.dll");



		REL::Relocation<uintptr_t> D3D11CreateDeviceAndSwapChainAddress{ REL::ID(438126) };
		Utilities::CreateAndEnableHook((LPVOID)D3D11CreateDeviceAndSwapChainAddress.address(), &D3DHooks::D3D11CreateDeviceAndSwapChain_Hook,
			reinterpret_cast<LPVOID*>(&m_D3D11CreateDeviceAndSwapChain_O), "D3D11CreateDeviceAndSwapChainAddress");

		
		return true;
	}

	
    
    bool D3DHooks::Initialize() 
	{
		
		logger::info("Initializing D3D11 hooks...");
		if (!m_SwapChain) {
			logger::error("Failed to get SwapChain");
			auto rendererData = RE::BSGraphics::RendererData::GetSingleton();
			m_SwapChain = (IDXGISwapChain*)static_cast<void*>(rendererData->renderWindow->swapChain);
			m_Device = (ID3D11Device*)static_cast<void*>(rendererData->device);
			m_Context = (ID3D11DeviceContext*)static_cast<void*>(rendererData->context);
		}
		// 获取窗口句柄并设置窗口过程
		DXGI_SWAP_CHAIN_DESC sd;
		m_SwapChain->GetDesc(&sd);
		::GetWindowRect(sd.OutputWindow, &oldRect);

		s_OriginalWndProc = reinterpret_cast<WNDPROC>(SetWindowLongPtr(sd.OutputWindow, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(hkWndProc)));

		// 获取虚函数表
		void** contextVTable = *(void***)m_Context;
		void** swapChainVTable = *(void***)m_SwapChain;

		void* drawIndexedFunc = contextVTable[12];
		void* presentFunc = swapChainVTable[8];  // 注意：Present是SwapChain的方法，不是Context的
		void* rsSetViewportsFunc = contextVTable[44];  // RSSetViewports - 索引44

		// 初始化ImGui管理器
		imguiMgr = ImGuiManager::GetSingleton();

		// 创建Hook

		if (isRenderDocDll) {
			if (rdoc_api && m_Device) {
				IDXGIDevice* pDXGIDevice = nullptr;
				if (SUCCEEDED(m_Device->QueryInterface(__uuidof(IDXGIDevice), (void**)&pDXGIDevice))) {
					rdoc_api->SetActiveWindow((void*)pDXGIDevice, sd.OutputWindow);
					logger::info("Set RenderDoc active window to {:x}", (uintptr_t)sd.OutputWindow);
					pDXGIDevice->Release();
				} else {
					logger::error("Failed to get DXGI device for RenderDoc");
				}
			}
		}


		Utilities::CreateAndEnableHook(presentFunc, reinterpret_cast<void*>(hkPresent), reinterpret_cast<void**>(&s_OriginalPresent), "Present");
		Utilities::CreateAndEnableHook(&ClipCursor, ClipCursorHook, reinterpret_cast<LPVOID*>(&phookClipCursor), "ClipCursorHook");
		Utilities::CreateAndEnableHook(rsSetViewportsFunc, reinterpret_cast<void*>(hkRSSetViewports), reinterpret_cast<void**>(&phookD3D11RSSetViewports), "RSSetViewportsHook");

		// Name all render targets for RenderDoc debugging
		NameAllRenderTargets();

		logger::info("D3D11 hooks initialized successfully");
		return true;
    }

    void WINAPI D3DHooks::hkDrawIndexed(ID3D11DeviceContext* pContext, UINT IndexCount, UINT StartIndexLocation, INT BaseVertexLocation)
	{
		if (isSelfDrawCall)
			return phookD3D11DrawIndexed(pContext, IndexCount, StartIndexLocation, BaseVertexLocation);

		bool isScopeQuad = IsScopeQuadBeingDrawn(pContext, IndexCount);

		if (isScopeQuad) {
			D3DPERF_BeginEvent(0xFFFF00FF, L"TTS_ScopeQuad_Detected");

			CacheAllStates();
			
			// 保存索引数量用于后续 SetScopeTexture 绘制
			s_ScopeQuadIndexCount = IndexCount;
			s_ScopeQuadStartIndexLocation = StartIndexLocation;
			s_ScopeQuadBaseVertexLocation = BaseVertexLocation;
			
			// 保存当前 viewport (Upscaling 兼容)
			// 顶点坐标是基于这个 viewport 计算的，渲染到 RT4 时需要使用相同的 viewport
			UINT vpCount = 1;
			pContext->RSGetViewports(&vpCount, &s_ScopeQuadOriginalViewport);
			s_HasScopeQuadViewport = (vpCount > 0);
			
			D3DPERF_EndEvent();
			
			if (ImGuiManager::GetSingleton()->IsMenuOpen() && !Utilities::IsPlayerInADS()) {
				return phookD3D11DrawIndexed(pContext, IndexCount, StartIndexLocation, BaseVertexLocation);
			}
			
			return;  // 跳过原始绘制，后续在 SetScopeTexture 中用自定义 shader 绘制
		} else {
			return phookD3D11DrawIndexed(pContext, IndexCount, StartIndexLocation, BaseVertexLocation);
		}
	}


	bool D3DHooks::LoadAimTexture(const std::string& path)
	{
		return D3DResourceManager::GetSingleton()->LoadReticleTexture(m_Device, path);
	}

	ID3D11ShaderResourceView* D3DHooks::LoadAimSRV(const std::string& path)
	{
		if (D3DResourceManager::GetSingleton()->LoadReticleTexture(m_Device, path)) {
			return D3DResourceManager::GetSingleton()->GetReticleSRV();
		}
		return nullptr;
	}

	void D3DHooks::CacheAllStates()
	{
		CacheIAState(m_Context);
		CacheVSState(m_Context);
		CacheRSState(m_Context);
		s_HasCachedState = true;
	}

	void D3DHooks::RestoreAllCachedStates()
	{
		if (s_HasCachedState) {
			RestoreIAState(m_Context);
			RestoreVSState(m_Context);
			RestoreRSState(m_Context);

			// 清除缓存
			s_CachedIAState.Clear();
			s_CachedVSState.Clear();
			s_CachedRSState.Clear();
			s_HasCachedState = false;
		}
	}

	bool D3DHooks::IsTargetDrawCall(const BufferInfo& vertexInfo, const BufferInfo& indexInfo, UINT indexCount)
	{
		UINT scopeNodeIndexCount = ScopeCamera::GetScopeNodeIndexCount();
		if (scopeNodeIndexCount <= 0)
			return false;

		// 检查 buffer size 是否匹配目标值
		bool bufferSizeMatch = (indexInfo.desc.ByteWidth == TARGET_BUFFER_SIZE && vertexInfo.desc.ByteWidth == TARGET_BUFFER_SIZE);
		bool strideMatch = (vertexInfo.stride == TARGET_STRIDE || vertexInfo.stride == 24);

		return strideMatch && indexCount == scopeNodeIndexCount && bufferSizeMatch;
	}

	bool D3DHooks::IsTargetDrawCall(std::vector<BufferInfo> vertexInfos, const BufferInfo& indexInfo, UINT indexCount)
	{
		UINT strideCount = 0;
		for (auto& info : vertexInfos) {
			strideCount += info.stride;
		}
		return strideCount == TARGET_STRIDE && indexCount == TARGET_INDEX_COUNT
		       //&& indexInfo.offset == 2133504
		       && indexInfo.desc.ByteWidth == TARGET_BUFFER_SIZE && vertexInfos[0].desc.ByteWidth == TARGET_BUFFER_SIZE;
	}

	UINT D3DHooks::GetVertexBuffersInfo(
		ID3D11DeviceContext* pContext,
		std::vector<BufferInfo>& outInfos,
		UINT maxSlotsToCheck)
	{
		outInfos.clear();

		// 1. 直接尝试获取所有可能的槽位
		std::vector<ID3D11Buffer*> buffers(maxSlotsToCheck);
		std::vector<UINT> strides(maxSlotsToCheck);
		std::vector<UINT> offsets(maxSlotsToCheck);

		pContext->IAGetVertexBuffers(0, maxSlotsToCheck, buffers.data(), strides.data(), offsets.data());

		// 2. 计算实际绑定的缓冲区数量
		UINT actualCount = 0;
		for (UINT i = 0; i < maxSlotsToCheck; ++i) {
			if (buffers[i] != nullptr) {
				actualCount++;
			}
		}

		if (actualCount == 0)
			return 0;

		// 3. 填充输出结构
		outInfos.resize(actualCount);
		UINT validIndex = 0;
		for (UINT i = 0; i < maxSlotsToCheck && validIndex < actualCount; ++i) {
			if (buffers[i] != nullptr) {
				outInfos[validIndex].stride = strides[i];
				outInfos[validIndex].offset = offsets[i];
				buffers[i]->GetDesc(&outInfos[validIndex].desc);
				buffers[i]->Release();  // 释放获取的引用
				validIndex++;
			}
		}

		return actualCount;
	}

	bool D3DHooks::GetIndexBufferInfo(ID3D11DeviceContext* pContext, BufferInfo& outInfo)
	{
		Microsoft::WRL::ComPtr<ID3D11Buffer> indexBuffer;
		DXGI_FORMAT format;

		// 获取索引缓冲区
		pContext->IAGetIndexBuffer(&indexBuffer, &format, &outInfo.offset);

		if (!indexBuffer)
			return false;

		// 获取缓冲区描述
		indexBuffer->GetDesc(&outInfo.desc);

		// 将格式信息存入stride（因为索引缓冲区没有stride概念）
		outInfo.stride = (format == DXGI_FORMAT_R32_UINT) ? 4 : 2;

		return true;
	}

	// Half-float 转换辅助函数 (FO4 使用 half-float 存储 UV)
	static float HalfToFloat(uint16_t half)
	{
		uint32_t sign = (half & 0x8000) << 16;
		uint32_t exponent = (half >> 10) & 0x1F;
		uint32_t mantissa = half & 0x3FF;

		if (exponent == 0) {
			// Subnormal or zero
			if (mantissa == 0) {
				uint32_t result = sign;
				return *reinterpret_cast<float*>(&result);
			}
			// Subnormal - normalize
			while ((mantissa & 0x400) == 0) {
				mantissa <<= 1;
				exponent--;
			}
			exponent++;
			mantissa &= 0x3FF;
		} else if (exponent == 31) {
			// Inf or NaN
			uint32_t result = sign | 0x7F800000 | (mantissa << 13);
			return *reinterpret_cast<float*>(&result);
		}

		exponent = exponent + (127 - 15);
		uint32_t result = sign | (exponent << 23) | (mantissa << 13);
		return *reinterpret_cast<float*>(&result);
	}

	// 检查顶点缓冲区是否包含 TTS Marker UV
	// FO4 顶点格式 (stride=28, Full Prec): Position(12) + BitangentX(4) + UV(4) + Normal(4) + Tangent(4)
	// UV 位于偏移 16 字节处，使用 half-float 格式
	static constexpr UINT TTS_UV_OFFSET = 16;  // Position(12) + BitangentX(4)
	Microsoft::WRL::ComPtr<ID3D11Buffer> D3DHooks::s_StagingBuffer = nullptr;

	bool D3DHooks::HasTTSMarkerUV(ID3D11DeviceContext* pContext, UINT stride, UINT offset)
	{
		// 只有 stride=28 的格式我们知道 UV 的位置
		if (stride != 28)
			return false;

		// 获取顶点缓冲区
		ID3D11Buffer* vertexBuffer = nullptr;
		UINT vbStride = 0, vbOffset = 0;
		pContext->IAGetVertexBuffers(0, 1, &vertexBuffer, &vbStride, &vbOffset);
		
		if (!vertexBuffer)
			return false;

		// 获取缓冲区描述
		D3D11_BUFFER_DESC bufferDesc;
		vertexBuffer->GetDesc(&bufferDesc);

		// 安全检查：防止读取越界
		// 我们需要读取 stride 字节，从 vbOffset 开始
		if (vbOffset + stride > bufferDesc.ByteWidth) {
			vertexBuffer->Release();
			return false;
		}

		// 如果缓存的 staging buffer 还没创建，或者丢失了，就创建一个
		if (!s_StagingBuffer) {
			ID3D11Device* device = nullptr;
			pContext->GetDevice(&device);
			if (!device) {
				vertexBuffer->Release();
				return false;
			}

			D3D11_BUFFER_DESC stagingDesc = {};
			stagingDesc.ByteWidth = 28; // 我们只关心 stride=28 的情况
			stagingDesc.Usage = D3D11_USAGE_STAGING;
			stagingDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
			stagingDesc.BindFlags = 0;

			HRESULT hr = device->CreateBuffer(&stagingDesc, nullptr, s_StagingBuffer.GetAddressOf());
			device->Release();

			if (FAILED(hr)) {
				vertexBuffer->Release();
				return false;
			}
		}

		// 从顶点缓冲区复制第一个顶点到 staging buffer
		D3D11_BOX srcBox = {};
		srcBox.left = vbOffset;
		srcBox.right = vbOffset + stride;
		srcBox.top = 0;
		srcBox.bottom = 1;
		srcBox.front = 0;
		srcBox.back = 1;

		pContext->CopySubresourceRegion(s_StagingBuffer.Get(), 0, 0, 0, 0, vertexBuffer, 0, &srcBox);

		// 映射 staging buffer 读取数据
		D3D11_MAPPED_SUBRESOURCE mapped;
		HRESULT hr = pContext->Map(s_StagingBuffer.Get(), 0, D3D11_MAP_READ, 0, &mapped);
		
		bool hasMarker = false;
		if (SUCCEEDED(hr)) {
			// UV 在偏移 TTS_UV_OFFSET 字节处，使用 half-float 格式
			const uint8_t* data = static_cast<const uint8_t*>(mapped.pData);
			const uint16_t* uvData = reinterpret_cast<const uint16_t*>(data + TTS_UV_OFFSET);
			
			float u = HalfToFloat(uvData[0]);
			float v = HalfToFloat(uvData[1]);
			
			// 检查是否匹配 TTS Marker UV
			hasMarker = (std::abs(u - TTS_MARKER_UV_U) < TTS_MARKER_UV_TOLERANCE &&
			             std::abs(v - TTS_MARKER_UV_V) < TTS_MARKER_UV_TOLERANCE);
			
			pContext->Unmap(s_StagingBuffer.Get(), 0);
		}

		vertexBuffer->Release();

		return hasMarker;
	}
    
	bool D3DHooks::IsScopeQuadBeingDrawn(ID3D11DeviceContext* pContext, UINT IndexCount)
	{
		if (!s_isForwardStage)
			return false;

		auto playerCharacter = RE::PlayerCharacter::GetSingleton();
		if (!playerCharacter || !playerCharacter->Get3D())
			return false;

		std::vector<BufferInfo> vertexInfo;
		BufferInfo indexInfo;
		if (!GetVertexBuffersInfo(pContext, vertexInfo) || !GetIndexBufferInfo(pContext, indexInfo))
			return false;
		
		if (vertexInfo.empty())
			return false;

		// ===== 主检测方法: TTS Marker UV =====
		// 检查第一个顶点的 UV 是否为 TTS 标记值 (0.415411, 0.189191)
		if (HasTTSMarkerUV(pContext, vertexInfo[0].stride, vertexInfo[0].offset))
		{
			return true;
		}

		// ===== 回退检测方法: 几何特征匹配 =====
		// 如果 NIF 还没有设置标记 UV，使用传统的几何匹配方法
		// 检查 buffer size, stride 和 index count
		if (IsTargetDrawCall(vertexInfo[0], indexInfo, IndexCount))
		{
			return true;
		}

		return false;
	}
	
	bool D3DHooks::IsScopeQuadBeingDrawnShape(ID3D11DeviceContext* pContext, UINT IndexCount)
	{
		if (s_isForwardStage)
			return false;

		auto playerCharacter = RE::PlayerCharacter::GetSingleton();
		if (!playerCharacter || !playerCharacter->Get3D())
			return false;

		std::vector<BufferInfo> vertexInfo;
		BufferInfo indexInfo;
		if (!GetVertexBuffersInfo(pContext, vertexInfo) || !GetIndexBufferInfo(pContext, indexInfo))
			return false;

		 // Get pixel shader resources to check for the textures
		ID3D11ShaderResourceView* psSRVs[3] = { nullptr };
		pContext->PSGetShaderResources(0, 3, psSRVs);

		bool foundTargetTextures = true;

		// Check the three SRVs
		for (int i = 0; i < 3; i++) {
			if (!psSRVs[i]) {
				foundTargetTextures = false;
				break;
			}

			// Get the resource from SRV
			ID3D11Resource* resource = nullptr;
			psSRVs[i]->GetResource(&resource);

			if (!resource) {
				foundTargetTextures = false;
				break;
			}

			// Check if it's a texture 2D
			ID3D11Texture2D* texture = nullptr;
			HRESULT hr = resource->QueryInterface(__uuidof(ID3D11Texture2D), (void**)&texture);
			resource->Release();

			if (FAILED(hr) || !texture) {
				foundTargetTextures = false;
				break;
			}

			// Get texture description to check dimensions and format
			D3D11_TEXTURE2D_DESC desc;
			texture->GetDesc(&desc);
			texture->Release();

			// Check if dimensions are 1x1 and format is R8G8B8A8_UNORM
			if (desc.Width != 1 || desc.Height != 1 ||
				desc.Format != DXGI_FORMAT_R8G8B8A8_UNORM) {
				foundTargetTextures = false;
				break;
			}
		}

		// Release the SRVs
		for (int i = 0; i < 3; i++) {
			if (psSRVs[i]) {
				psSRVs[i]->Release();
			}
		}

		// Return true if this is our target draw call
		return foundTargetTextures;
	}

    void D3DHooks::SetScopeTexture(ID3D11DeviceContext* pContext)
	{
		// 确保我们有有效的纹理
		if (!RenderUtilities::GetSecondPassColorTexture()) {
			logger::error("No second pass texture available");
			return;
		}

		// 获取D3D11设备
		ID3D11Device* device = nullptr;
		pContext->GetDevice(&device);
		if (!device) {
			logger::error("Failed to get D3D11 device in SetScopeTexture");
			return;
		}

		auto rendererData = RE::BSGraphics::RendererData::GetSingleton();
		auto rendererState = RE::BSGraphics::State::GetSingleton();
		
		UINT screenWidth = rendererState.backBufferWidth;
		UINT screenHeight = rendererState.backBufferHeight;
		
		// DLSS/FSR3 Upscaling 兼容：使用 scope quad 检测时捕获的原始 viewport
		// 这样 shader 的 UV 计算与 SV_POSITION 坐标空间匹配
		// 注意：s_ScopeQuadOriginalViewport 是在 hkDrawIndexed 中检测到 scope quad 时捕获的
		float viewportWidth = static_cast<float>(screenWidth);
		float viewportHeight = static_cast<float>(screenHeight);
		
		if (s_HasScopeQuadViewport && 
			s_ScopeQuadOriginalViewport.Width > 0 && s_ScopeQuadOriginalViewport.Height > 0) {
			// 使用检测时捕获的 viewport，与渲染时设置的 viewport 一致
			viewportWidth = s_ScopeQuadOriginalViewport.Width;
			viewportHeight = s_ScopeQuadOriginalViewport.Height;
		} else {
			// Fallback: 使用 RT4/FirstPass viewport
			D3D11_VIEWPORT firstPassViewport = {};
			if (RenderUtilities::GetFirstPassViewport(firstPassViewport) && 
				firstPassViewport.Width > 0 && firstPassViewport.Height > 0) {
				viewportWidth = firstPassViewport.Width;
				viewportHeight = firstPassViewport.Height;
			}
		}

		// 获取视差所需的数据
		// 视差效果应该基于瞄具朝向的帧间变化（模拟呼吸/晃动）
		// 静止时变化量为0，所以视差为0；晃动时产生动态视差
		
		RE::NiPoint3 cameraPos(0, 0, 0);
		RE::NiPoint3 lastCameraPos(0, 0, 0);
		RE::NiPoint3 scopePos(0, 0, 0);
		RE::NiPoint3 lastScopePos(0, 0, 0);
		
		auto scopeCamera = ScopeCamera::GetScopeCamera();
		
		if (scopeCamera) {
			// 使用瞄具摄像机的位置作为参考中心
			scopePos = scopeCamera->world.translate;
			lastScopePos = scopeCamera->previousWorld.translate;
			
			// 获取瞄具摄像机当前和上一帧的前方向（Y轴 in NIF convention）
			auto& currentRot = scopeCamera->world.rotate;
			auto& lastRot = scopeCamera->previousWorld.rotate;
			
			// 当前帧的前方向
			RE::NiPoint3 currentForward(currentRot.entry[1].x, currentRot.entry[1].y, currentRot.entry[1].z);
			// 上一帧的前方向
			RE::NiPoint3 lastForward(lastRot.entry[1].x, lastRot.entry[1].y, lastRot.entry[1].z);
			
			// Decay factor for accumulated offset
			constexpr float decayFactor = 0.95f;
			constexpr float sensitivityFactor = 500.0f;
			
			if (!s_ParallaxInitialized) {
				s_ParallaxInitialized = true;
				s_AccumulatedParallaxOffset = RE::NiPoint3(0, 0, 0);
				// Don't accumulate on first frame - just initialize
			} else {
				RE::NiPoint3 forwardDelta = currentForward - lastForward;
				
				float deltaLen = std::sqrt(forwardDelta.x * forwardDelta.x + 
				                           forwardDelta.y * forwardDelta.y + 
				                           forwardDelta.z * forwardDelta.z);
				if (deltaLen > 0.1f) {
					// Reset and skip this frame
					s_AccumulatedParallaxOffset = RE::NiPoint3(0, 0, 0);
				} else {
					// Normal case: accumulate offset
					s_AccumulatedParallaxOffset = s_AccumulatedParallaxOffset * decayFactor + forwardDelta * sensitivityFactor;
				}
			}
			
			// 限制最大偏移量
			float maxOffset = 50.0f;
			float offsetLen = std::sqrt(s_AccumulatedParallaxOffset.x * s_AccumulatedParallaxOffset.x + 
			                           s_AccumulatedParallaxOffset.y * s_AccumulatedParallaxOffset.y + 
			                           s_AccumulatedParallaxOffset.z * s_AccumulatedParallaxOffset.z);
			if (offsetLen > maxOffset) {
				float scale = maxOffset / offsetLen;
				s_AccumulatedParallaxOffset.x *= scale;
				s_AccumulatedParallaxOffset.y *= scale;
				s_AccumulatedParallaxOffset.z *= scale;
			}
		
			cameraPos = scopePos + s_AccumulatedParallaxOffset;
			lastCameraPos = lastScopePos;  // 上一帧相对位置接近0
		}

		// 获取纹理描述
		D3D11_TEXTURE2D_DESC srcTexDesc;
		RenderUtilities::GetSecondPassColorTexture()->GetDesc(&srcTexDesc);

		// 使用 D3DResourceManager 管理资源
		auto resManager = D3DResourceManager::GetSingleton();
		if (!resManager->EnsureStagingTexture(device, &srcTexDesc)) {
			logger::error("Failed to ensure staging texture");
			return;
		}
		
		ID3D11Texture2D* stagingTexture = resManager->GetStagingTexture();
		ID3D11ShaderResourceView* stagingSRV = resManager->GetScopeTextureView();

		// 复制/解析纹理内容
		if (srcTexDesc.SampleDesc.Count > 1) {
			pContext->ResolveSubresource(
				stagingTexture, 0,
				RenderUtilities::GetSecondPassColorTexture(), 0,
				srcTexDesc.Format);
		} else {
			ID3D11Device* device = nullptr;
			pContext->GetDevice(&device);
			if (device) {
				RenderUtilities::SafeCopyTexture(pContext, device, stagingTexture, RenderUtilities::GetSecondPassColorTexture());
				device->Release();
			} else {
				pContext->CopyResource(stagingTexture, RenderUtilities::GetSecondPassColorTexture());
			}
		}

		RestoreAllCachedStates();

		// 使用渲染分辨率而非显示分辨率 (Fallout4Upscaler兼容)
		ScopeConstantBuffer newCBData = {};
		// screen* 必须是【渲染目标(RT4)】的真实像素尺寸，viewport* 才是【绘制区域】。
		// 作者原来把两者都设成 viewport 尺寸，在原生渲染下（3840==3840）没问题，
		// 但超分时 viewport 被降到 1920x1080，而 RT4 仍是 3840x2160 —— shader 按
		// 1920 计算，内容就只覆盖 RT4 的四分之一（用户实测：画中画正好是整个屏幕
		// 的四分之一大）。这里把 screen* 改成 RT4 真实尺寸即可。
		{
			UINT cbRt4W = 0, cbRt4H = 0;
			if (auto rtv = rendererData->renderTargets[4].rtView) {
				Microsoft::WRL::ComPtr<ID3D11RenderTargetView> rt4View = (ID3D11RenderTargetView*)rtv;
				Microsoft::WRL::ComPtr<ID3D11Resource> rt4Res;
				rt4View->GetResource(rt4Res.GetAddressOf());
				Microsoft::WRL::ComPtr<ID3D11Texture2D> rt4Tex2d;
				if (rt4Res && SUCCEEDED(rt4Res.As(&rt4Tex2d)) && rt4Tex2d) {
					D3D11_TEXTURE2D_DESC d = {};
					rt4Tex2d->GetDesc(&d);
					cbRt4W = d.Width;
					cbRt4H = d.Height;
				}
			}
			newCBData.screenWidth = cbRt4W ? static_cast<float>(cbRt4W) : viewportWidth;
			newCBData.screenHeight = cbRt4H ? static_cast<float>(cbRt4H) : viewportHeight;
		}
		newCBData.viewportWidth = viewportWidth;
		newCBData.viewportHeight = viewportHeight;
		newCBData.cameraPosition[0] = cameraPos.x;
		newCBData.cameraPosition[1] = cameraPos.y;
		newCBData.cameraPosition[2] = cameraPos.z;
		newCBData.scopePosition[0] = scopePos.x;
		newCBData.scopePosition[1] = scopePos.y;
		newCBData.scopePosition[2] = scopePos.z;
		// 保持用户设置的基础准星缩放不变
		newCBData.reticleScale = s_ReticleScale;
		newCBData.reticleOffsetX = s_ReticleOffsetX;
		newCBData.reticleOffsetY = s_ReticleOffsetY;
		
		// 计算准星放大倍率 - 若启用了随瞄具放大，则根据当前FOV计算倍率
		float zoomScale = 1.0f;
		if (s_ScaleReticleWithZoom) {
			// 获取当前实际 FOV 和基准 FOV
			float currentFOV = ScopeCamera::GetTargetFOV();
			// 使用玩家的 worldFOV 作为基准
			float baseFOV = ScopeCamera::GetBaseFOV();
			
			// 放大倍率 = 基准FOV / 当前FOV
			// 例如：baseFOV=70, currentFOV=35 -> zoomScale=2.0 (2倍放大)
			if (currentFOV > 1.0f) {
				zoomScale = baseFOV / currentFOV;
			}
		}
		newCBData.reticleZoomScale = zoomScale;
		newCBData.enableNightVision = s_EnableNightVision;

		
		// 添加球形畸变参数到变化检测中
		newCBData.sphericalDistortionStrength = s_SphericalDistortionStrength;
		newCBData.sphericalDistortionRadius = s_SphericalDistortionRadius;
		newCBData.sphericalDistortionCenter[0] = s_SphericalDistortionCenterX;
		newCBData.sphericalDistortionCenter[1] = s_SphericalDistortionCenterY;
		newCBData.enableSphericalDistortion = s_EnableSphericalDistortion;
		newCBData.enableChromaticAberration = s_EnableChromaticAberration;
		newCBData.brightnessBoost = 1.0f;   // No additional brightness boost (gamma correction only)
		newCBData.ambientOffset = 0.0f;     // Unused

		// 检查是否需要更新常量缓冲区
		static int s_ForceUpdateCounter = 0;
		bool forceUpdate = (s_ForceUpdateCounter++ % 60 == 0); // 每60帧强制更新一次
		bool needsUpdate = s_CachedConstantBufferData.NeedsUpdate(newCBData);

		if (needsUpdate || forceUpdate) {
            // Update additional data
            newCBData.lastCameraPosition[0] = lastCameraPos.x;
            newCBData.lastCameraPosition[1] = lastCameraPos.y;
            newCBData.lastCameraPosition[2] = lastCameraPos.z;

            newCBData.lastScopePosition[0] = lastScopePos.x;
            newCBData.lastScopePosition[1] = lastScopePos.y;
            newCBData.lastScopePosition[2] = lastScopePos.z;

			// 计算相对于瞄具的视图变换矩阵
			DirectX::XMFLOAT4X4 rotationMatrix;

			auto scopeCamera = ScopeCamera::GetScopeCamera();
			if (scopeCamera) {
				auto& scopeRot = scopeCamera->world.rotate;
				// 构建视图变换矩阵
				rotationMatrix._11 = scopeRot.entry[0].x; rotationMatrix._12 = scopeRot.entry[0].y; rotationMatrix._13 = scopeRot.entry[0].z; rotationMatrix._14 = 0.0f;
				rotationMatrix._21 = scopeRot.entry[1].x; rotationMatrix._22 = scopeRot.entry[1].y; rotationMatrix._23 = scopeRot.entry[1].z; rotationMatrix._24 = 0.0f;
				rotationMatrix._31 = scopeRot.entry[2].x; rotationMatrix._32 = scopeRot.entry[2].y; rotationMatrix._33 = scopeRot.entry[2].z; rotationMatrix._34 = 0.0f;
				rotationMatrix._41 = 0.0f; rotationMatrix._42 = 0.0f; rotationMatrix._43 = 0.0f; rotationMatrix._44 = 1.0f;
			} else {
				memset(&rotationMatrix, 0, sizeof(rotationMatrix));
				rotationMatrix._11 = rotationMatrix._22 = rotationMatrix._33 = rotationMatrix._44 = 1.0f;
			}

            newCBData.parallaxStrength = s_ParallaxStrength;
            newCBData.parallaxSmoothing = s_ParallaxSmoothing;
            newCBData.exitPupilRadius = s_ExitPupilRadius;
            newCBData.exitPupilSoftness = s_ExitPupilSoftness;
            newCBData.vignetteStrength = s_VignetteStrength;
            newCBData.vignetteRadius = s_VignetteRadius;
            newCBData.vignetteSoftness = s_VignetteSoftness;
            newCBData.eyeReliefDistance = s_EyeReliefDistance;
            newCBData.enableParallax = s_EnableParallax;

            newCBData.nightVisionIntensity = s_EnableNightVision ? s_NightVisionIntensity : 0.0f;
            newCBData.nightVisionNoiseScale = s_NightVisionNoiseScale;
            newCBData.nightVisionNoiseAmount = s_NightVisionNoiseAmount;
            newCBData.nightVisionGreenTint = s_NightVisionGreenTint;

            // 高级视差参数
            newCBData.parallaxFogRadius = s_ParallaxFogRadius;
            newCBData.parallaxMaxTravel = s_ParallaxMaxTravel;
            newCBData.reticleParallaxStrength = s_ReticleParallaxStrength;

            auto camMat = RE::PlayerCamera::GetSingleton()->cameraRoot->local.rotate.entry[0];
            memcpy_s(&newCBData.CameraRotation, sizeof(newCBData.CameraRotation), &rotationMatrix, sizeof(rotationMatrix));

            // Use D3DResourceManager to update Buffer
            resManager->UpdateConstantBuffer(pContext, newCBData);
            
            s_CachedConstantBufferData.UpdateFrom(newCBData);
		}

		float blendFactor[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
		pContext->OMSetBlendState(nullptr, blendFactor, 0xffffffff);

		ID3D11Buffer* cb = resManager->GetConstantBuffer();
		pContext->PSSetConstantBuffers(0, 1, &cb);
		
		pContext->PSSetShader(resManager->GetScopePixelShader(), nullptr, 0);

		// 设置纹理资源和采样器
        ID3D11ShaderResourceView* views[2] = { stagingSRV, resManager->GetReticleSRV() };
		pContext->PSSetShaderResources(0, 2, views);
		
		// 设置采样器（s0用于主纹理）
		ID3D11SamplerState* samplers[1] = { resManager->GetSamplerState() };
        pContext->PSSetSamplers(0, 1, samplers);

		// === 绘制 ScopeQuad 并写入 Stencil ===
		// ScopeQuad 在 hkDrawIndexed 中被跳过了，需要在这里用自定义 shader 绘制
		// 注意：此 DrawIndexed 同时完成 stencil 写入和实际渲染（已合并两个调用）
		D3DPERF_BeginEvent(0xFF00FFFF, L"TTS_ScopeQuad_DrawWithStencil");  // 青色标记
		
		// 使用 ScopeCamera 获取正确的索引数量（与原 RestoreFirstPass 一致）
		int scopeNodeIndexCount = ScopeCamera::GetScopeNodeIndexCount();
		if (scopeNodeIndexCount > 0) {
			// 获取支持 stencil 的 DSV
			auto rendererData = RE::BSGraphics::RendererData::GetSingleton();
			ID3D11DepthStencilView* stencilDSV = nullptr;
			if (rendererData && rendererData->depthStencilTargets[2].dsView[0]) {
				stencilDSV = (ID3D11DepthStencilView*)rendererData->depthStencilTargets[2].dsView[0];
			}
			
			// 如果没有有效的 DSV，无法写入深度和 stencil
			if (!stencilDSV) {
				logger::warn("SetScopeTexture: No valid DSV available, skipping depth/stencil write");
				D3DPERF_EndEvent();
				device->Release();
				return;
			}

			// 使用 RT4 作为渲染目标 (Upscaling 兼容)
			// Upscaling MOD 从 RT4 复制数据，所以必须渲染到 RT4
			ID3D11RenderTargetView* rt4RTV = (ID3D11RenderTargetView*)rendererData->renderTargets[4].rtView;

			// RT4 的真实像素尺寸（诊断和下面的 viewport 缩放都要用，所以放外层）
			UINT rt4W = 0, rt4H = 0;
			if (rt4RTV) {
				Microsoft::WRL::ComPtr<ID3D11Resource> rt4Res;
				rt4RTV->GetResource(rt4Res.GetAddressOf());
				Microsoft::WRL::ComPtr<ID3D11Texture2D> rt4Tex2d;
				if (rt4Res && SUCCEEDED(rt4Res.As(&rt4Tex2d)) && rt4Tex2d) {
					D3D11_TEXTURE2D_DESC rt4Desc = {};
					rt4Tex2d->GetDesc(&rt4Desc);
					rt4W = rt4Desc.Width;
					rt4H = rt4Desc.Height;
				}
			}

			// ------------------------------------------------------------------
			// [DIAG] 画幅诊断：把尺寸链打出来（每 120 帧一次，避免刷屏）
			//
			// 目的是回答一个问题：游戏绘制瞄具四边形时，当前 viewport 与 RT4 的
			// 尺寸是否一致？
			//   - 一致   => 顶点坐标空间与 RT 匹配，画幅应该正常
			//   - 不一致 => viewport 变换把画面缩放到 RT 的左上角，画幅缩小/错位
			//
			// Upscaling(99130) 下作者这套能工作；Upscaling Custom(108772) 把
			// RenderEffectRange 挪到 Late 阶段，此时 viewport 可能已被改回显示分辨率，
			// 这个不一致就可能发生。先量数据再改，不猜。
			// ------------------------------------------------------------------
			{
				static int s_diagTick = 0;
				if ((s_diagTick++ % 120) == 0) {
					D3D11_VIEWPORT probeVp = {};
					UINT probeCount = 1;
					pContext->RSGetViewports(&probeCount, &probeVp);

					// rt4W/rt4H 已在外层算好，这里直接用
					auto rs = RE::BSGraphics::State::GetSingleton();
					auto* srm = ScopeRenderingManager::GetSingleton();
					const unsigned bbW = rs.backBufferWidth;
					const unsigned bbH = rs.backBufferHeight;

					// 直接写独立文件：spdlog 的 sink 在本工程里不一定及时 flush
					char line[512];
					sprintf_s(line,
						"captured=%ux%u hasCaptured=%d rt4=%ux%u currentVp=%ux%u backBuffer=%ux%u upscaling=%d\r\n",
						static_cast<unsigned>(s_ScopeQuadOriginalViewport.Width),
						static_cast<unsigned>(s_ScopeQuadOriginalViewport.Height),
						s_HasScopeQuadViewport ? 1 : 0,
						rt4W, rt4H,
						static_cast<unsigned>(probeVp.Width), static_cast<unsigned>(probeVp.Height),
						bbW, bbH,
						(srm && srm->IsUpscalingActive()) ? 1 : 0);

					HANDLE hf = ::CreateFileA("E:\\Custom Fallout4\\tts-viewport.txt",
						GENERIC_WRITE, FILE_SHARE_READ, nullptr,
						CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
					if (hf != INVALID_HANDLE_VALUE) {
						DWORD written = 0;
						::WriteFile(hf, line, (DWORD)strlen(line), &written, nullptr);
						::CloseHandle(hf);
					}
				}
			}

			
			// 备份当前 RTV/DSV
			Microsoft::WRL::ComPtr<ID3D11RenderTargetView> currentRTV;
			Microsoft::WRL::ComPtr<ID3D11DepthStencilView> currentDSV;
			pContext->OMGetRenderTargets(1, currentRTV.GetAddressOf(), currentDSV.GetAddressOf());

			// 备份原始 DSS
			Microsoft::WRL::ComPtr<ID3D11DepthStencilState> oldDSS;
			UINT oldStencilRef;
			pContext->OMGetDepthStencilState(oldDSS.GetAddressOf(), &oldStencilRef);
			
			// 备份当前 viewport
			UINT numViewports = D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE;
			D3D11_VIEWPORT oldViewports[D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE];
			pContext->RSGetViewports(&numViewports, oldViewports);

			// 创建写入 stencil 和深度的 DSS
			// 深度测试总是通过，这样 ScopeQuad 不会被场景深度遮挡
			// 但我们仍然写入深度，以便后续效果（如景深、SSAO）可以正确处理瞄具区域
			D3D11_DEPTH_STENCIL_DESC dssDesc;
			ZeroMemory(&dssDesc, sizeof(dssDesc));
			dssDesc.DepthEnable = TRUE;   // 启用深度，但使用 ALWAYS 比较确保不被遮挡
			dssDesc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;  // 写入深度值
			dssDesc.DepthFunc = D3D11_COMPARISON_ALWAYS;  // 总是通过深度测试
			dssDesc.StencilEnable = TRUE;
			dssDesc.StencilReadMask = 0xFF;
			dssDesc.StencilWriteMask = 0xFF;  // 允许写入 stencil
			dssDesc.FrontFace.StencilFailOp = D3D11_STENCIL_OP_KEEP;
			dssDesc.FrontFace.StencilDepthFailOp = D3D11_STENCIL_OP_KEEP;
			dssDesc.FrontFace.StencilPassOp = D3D11_STENCIL_OP_REPLACE;  // 通过时替换为 ref
			dssDesc.FrontFace.StencilFunc = D3D11_COMPARISON_ALWAYS;    // 总是写入
			dssDesc.BackFace = dssDesc.FrontFace;

			Microsoft::WRL::ComPtr<ID3D11DepthStencilState> stencilWriteDSS;
			if (SUCCEEDED(device->CreateDepthStencilState(&dssDesc, stencilWriteDSS.GetAddressOf()))) {
				// 设置 viewport：保持作者原样（直接用捕获的 viewport）。
				//
				// 曾试过按 RT4/captured 的比例放大 viewport，实测失败：
				//   - 镜片被顶飞（viewport 参与几何变换）
				//   - 画中画大小没变（内容尺寸不由 viewport 决定，而在 UV/采样侧）
				// 所以这里保持原样，修复应在 UV 那一侧。
				if (s_HasScopeQuadViewport) {
					pContext->RSSetViewports(1, &s_ScopeQuadOriginalViewport);
				}
				
				// 绑定 RT4 + 支持 stencil 的 DSV
				pContext->OMSetRenderTargets(1, &rt4RTV, stencilDSV);

				// 设置 stencil ref = 127
				pContext->OMSetDepthStencilState(stencilWriteDSS.Get(), 127);

				// 绘制 ScopeQuad（同时写入 stencil 和渲染颜色）
				// 使用 StartIndexLocation=0, BaseVertexLocation=0（与原 RestoreFirstPass 一致）
				isSelfDrawCall = true;
				pContext->DrawIndexed(scopeNodeIndexCount, 0, 0);
				isSelfDrawCall = false;

				// 恢复原始 viewport
				if (numViewports > 0) {
					pContext->RSSetViewports(numViewports, oldViewports);
				}
				
				// 恢复原始 RTV/DSV 和 DSS
				pContext->OMSetRenderTargets(1, currentRTV.GetAddressOf(), currentDSV.Get());
				pContext->OMSetDepthStencilState(oldDSS.Get(), oldStencilRef);
			}
		}
		
		D3DPERF_EndEvent();

		// 确保释放设备引用
		device->Release();
	}

	// ===========================================================================
	// [RETICLE 验证] 屏幕中心绘制（D3D11 直绘，不经过 ImGui）
	//
	// 上一版在游戏画面里看不见，本版针对性做了三件事：
	//   1) 显式把渲染目标切到 swapchain 的 backbuffer —— 不再依赖 Present 时刻
	//      OM 里残留的是哪个 RT（TTS 自己会在 Present 前切换渲染目标）；
	//   2) 显式关闭深度测试与背面剔除 —— 避免被残留的 depth buffer 丢弃；
	//   3) 位置用常量缓冲在 NDC 里算，不依赖（也不修改）viewport。
	// 诊断日志写到 E:\Custom Fallout4\reticle-box.txt
	// ===========================================================================
	extern bool g_reticleVerifyADS;

	namespace ReticleBox
	{
		struct ReticleCB
		{
			float screenW, screenH;
			float centerX, centerY;
			float boxW, boxH;
			float alphaMul, pad1;
		};

		static ID3D11VertexShader* s_vs = nullptr;
		static ID3D11PixelShader* s_ps = nullptr;
		static ID3D11Buffer* s_cb = nullptr;
		static ID3D11BlendState* s_blend = nullptr;
		static ID3D11DepthStencilState* s_ds = nullptr;
		static ID3D11RasterizerState* s_rs = nullptr;
		static ID3D11SamplerState* s_sampler = nullptr;
		static ID3D11ShaderResourceView* s_srv = nullptr;
		static bool s_texFromFile = false;
		static ID3D11Texture2D* s_bbTex = nullptr;
		static ID3D11RenderTargetView* s_bbRTV = nullptr;
		static bool s_tried = false;
		static bool s_ok = false;
		static int s_logCount = 0;

		// 准星贴图：优先读 mod 里的 DDS（MO2 的 VFS 会把 Data\... 映射进虚拟目录），
		// 读不到就退化成 1x1 纯绿纹理 —— 一眼就能区分"贴图没加载"和"没画出来"。
		static const char* kReticlePath = "Data\\Textures\\TTS\\Reticle\\CSResRetGerman1.dds";

		static void Log(const char* fmt, ...)
		{
			char buf[1024];
			va_list args;
			va_start(args, fmt);
			vsnprintf_s(buf, sizeof(buf), _TRUNCATE, fmt, args);
			va_end(args);
			strcat_s(buf, "\r\n");
			HANDLE h = ::CreateFileA("E:\\Custom Fallout4\\reticle-box.txt",
				FILE_APPEND_DATA, FILE_SHARE_READ, nullptr,
				OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
			if (h != INVALID_HANDLE_VALUE) {
				DWORD w = 0;
				::WriteFile(h, buf, (DWORD)strlen(buf), &w, nullptr);
				::CloseHandle(h);
			}
		}

		static const char* kVS = R"(
struct VSOut { float4 pos : SV_POSITION; float2 uv : TEXCOORD0; };
cbuffer Cb : register(b0) {
    float screenW, screenH, centerX, centerY, boxW, boxH, alphaMul, pad1;
};
VSOut main(uint id : SV_VertexID) {
    float2 c[4]  = { float2(-0.5,-0.5), float2(0.5,-0.5), float2(-0.5,0.5), float2(0.5,0.5) };
    float2 uv[4] = { float2(0.0,1.0),  float2(1.0,1.0),  float2(0.0,0.0), float2(1.0,0.0) };
    float2 px = float2(centerX, centerY) + c[id] * float2(boxW, boxH);
    VSOut o;
    o.pos = float4( px.x / screenW * 2.0 - 1.0,
                    1.0 - px.y / screenH * 2.0,
                    0.5, 1.0 );
    o.uv = uv[id];
    return o;
}
)";

		static const char* kPS = R"(
cbuffer Cb : register(b0) {
    float screenW, screenH, centerX, centerY, boxW, boxH, alphaMul, pad1;
};
Texture2D    gTex  : register(t0);
SamplerState gSamp : register(s0);
float4 main(float4 pos : SV_POSITION, float2 uv : TEXCOORD0) : SV_Target {
    float4 c = gTex.Sample(gSamp, uv);
    return float4(c.rgb, c.a * alphaMul);
}
)";

		typedef HRESULT(WINAPI* D3DCompile_t)(LPCVOID, SIZE_T, LPCSTR, const D3D_SHADER_MACRO*,
			ID3DInclude*, LPCSTR, LPCSTR, UINT, UINT, ID3DBlob**, ID3DBlob**);

		// 从 DDS 读准星贴图（MO2 的 VFS 会把 Data\... 映射进虚拟目录）
		static HRESULT LoadReticleFromFile(ID3D11Device* dev)
		{
			wchar_t wpath[MAX_PATH] = {};
			if (::MultiByteToWideChar(CP_ACP, 0, kReticlePath, -1, wpath, MAX_PATH) <= 0)
				return E_FAIL;

			ID3D11ShaderResourceView* srv = nullptr;
			HRESULT hr = DirectX::CreateDDSTextureFromFile(dev, wpath, nullptr, &srv);
			if (SUCCEEDED(hr) && srv) {
				s_srv = srv;
				s_texFromFile = true;
				return S_OK;
			}
			if (srv) srv->Release();
			return FAILED(hr) ? hr : E_FAIL;
		}

		// 1x1 纯绿兜底纹理
		static HRESULT CreateSolidGreenSRV(ID3D11Device* dev)
		{
			const uint32_t px = 0xFF00FF00;  // R8G8B8A8: R=0 G=255 B=0 A=255
			D3D11_TEXTURE2D_DESC td = {};
			td.Width = 1;
			td.Height = 1;
			td.MipLevels = 1;
			td.ArraySize = 1;
			td.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
			td.SampleDesc.Count = 1;
			td.Usage = D3D11_USAGE_IMMUTABLE;
			td.BindFlags = D3D11_BIND_SHADER_RESOURCE;

			D3D11_SUBRESOURCE_DATA init = {};
			init.pSysMem = &px;
			init.SysMemPitch = sizeof(px);

			ID3D11Texture2D* tex = nullptr;
			HRESULT hr = dev->CreateTexture2D(&td, &init, &tex);
			if (FAILED(hr) || !tex) return FAILED(hr) ? hr : E_FAIL;

			hr = dev->CreateShaderResourceView(tex, nullptr, &s_srv);
			tex->Release();
			s_texFromFile = false;
			return hr;
		}

		static bool Init(ID3D11Device* dev)
		{
			if (s_tried) return s_ok;
			s_tried = true;

			HMODULE comp = ::LoadLibraryA("d3dcompiler_47.dll");
			if (!comp) { Log("[ReticleBox] d3dcompiler_47.dll load FAILED"); return false; }
			auto compile = (D3DCompile_t)::GetProcAddress(comp, "D3DCompile");
			if (!compile) { Log("[ReticleBox] D3DCompile not found"); return false; }

			ID3DBlob* vsBlob = nullptr;
			ID3DBlob* psBlob = nullptr;
			ID3DBlob* err = nullptr;

			if (FAILED(compile(kVS, strlen(kVS), "ReticleBoxVS", nullptr, nullptr, "main", "vs_5_0", 0, 0, &vsBlob, &err))) {
				Log("[ReticleBox] VS compile FAILED: %s", err ? (const char*)err->GetBufferPointer() : "?");
				if (err) err->Release();
				return false;
			}
			if (FAILED(compile(kPS, strlen(kPS), "ReticleBoxPS", nullptr, nullptr, "main", "ps_5_0", 0, 0, &psBlob, &err))) {
				Log("[ReticleBox] PS compile FAILED: %s", err ? (const char*)err->GetBufferPointer() : "?");
				if (err) err->Release();
				if (vsBlob) vsBlob->Release();
				return false;
			}

			HRESULT hrVs = dev->CreateVertexShader(vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), nullptr, &s_vs);
			HRESULT hrPs = dev->CreatePixelShader(psBlob->GetBufferPointer(), psBlob->GetBufferSize(), nullptr, &s_ps);
			vsBlob->Release();
			psBlob->Release();
			if (FAILED(hrVs) || FAILED(hrPs)) {
				Log("[ReticleBox] Create*Shader FAILED vs=0x%08X ps=0x%08X", (unsigned)hrVs, (unsigned)hrPs);
				return false;
			}

			D3D11_BUFFER_DESC cbd = {};
			cbd.ByteWidth = sizeof(ReticleCB);
			cbd.Usage = D3D11_USAGE_DYNAMIC;
			cbd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
			cbd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
			if (FAILED(dev->CreateBuffer(&cbd, nullptr, &s_cb))) {
				Log("[ReticleBox] CreateBuffer(CB) FAILED");
				return false;
			}

			// 准星是带 alpha 的贴图：常规 SrcAlpha/InvSrcAlpha 混合
			D3D11_BLEND_DESC bd = {};
			bd.RenderTarget[0].BlendEnable = TRUE;
			bd.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
			bd.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
			bd.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
			bd.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
			bd.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ZERO;
			bd.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
			bd.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
			if (FAILED(dev->CreateBlendState(&bd, &s_blend))) {
				Log("[ReticleBox] CreateBlendState FAILED");
				return false;
			}

			D3D11_SAMPLER_DESC sd = {};
			sd.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
			sd.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
			sd.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
			sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
			sd.ComparisonFunc = D3D11_COMPARISON_NEVER;
			sd.MinLOD = 0.0f;
			sd.MaxLOD = D3D11_FLOAT32_MAX;
			if (FAILED(dev->CreateSamplerState(&sd, &s_sampler))) {
				Log("[ReticleBox] CreateSamplerState FAILED");
				return false;
			}

			// 准星贴图（失败退化成 1x1 纯绿，便于区分"贴图没读到"）
			if (FAILED(LoadReticleFromFile(dev))) {
				Log("[ReticleBox] reticle DDS failed: %s -> fallback GREEN", kReticlePath);
				if (FAILED(CreateSolidGreenSRV(dev))) {
					Log("[ReticleBox] fallback green SRV FAILED");
					return false;
				}
			} else {
				Log("[ReticleBox] reticle DDS loaded: %s", kReticlePath);
			}

			D3D11_DEPTH_STENCIL_DESC dd = {};
			dd.DepthEnable = FALSE;
			dd.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
			dd.DepthFunc = D3D11_COMPARISON_ALWAYS;
			dd.StencilEnable = FALSE;
			if (FAILED(dev->CreateDepthStencilState(&dd, &s_ds))) {
				Log("[ReticleBox] CreateDepthStencilState FAILED");
				return false;
			}

			D3D11_RASTERIZER_DESC rd = {};
			rd.FillMode = D3D11_FILL_SOLID;
			rd.CullMode = D3D11_CULL_NONE;
			rd.DepthClipEnable = FALSE;
			rd.ScissorEnable = FALSE;
			if (FAILED(dev->CreateRasterizerState(&rd, &s_rs))) {
				Log("[ReticleBox] CreateRasterizerState FAILED");
				return false;
			}

			s_ok = true;
			Log("[ReticleBox] init OK (vs/ps/cb/blend/ds/rs)");
			return true;
		}

		static bool EnsureBackBuffer(IDXGISwapChain* pSwapChain, ID3D11Device* dev)
		{
			ID3D11Texture2D* tex = nullptr;
			if (FAILED(pSwapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&tex)) || !tex)
				return false;

			if (tex == s_bbTex && s_bbRTV) { tex->Release(); return true; }

			// 尺寸或纹理变了：重建
			if (s_bbRTV) { s_bbRTV->Release(); s_bbRTV = nullptr; }
			if (s_bbTex) { s_bbTex->Release(); s_bbTex = nullptr; }

			HRESULT hr = dev->CreateRenderTargetView(tex, nullptr, &s_bbRTV);
			if (FAILED(hr)) {
				Log("[ReticleBox] CreateRenderTargetView FAILED 0x%08X", (unsigned)hr);
				tex->Release();
				return false;
			}
			s_bbTex = tex;  // 接管引用
			return true;
		}

		static void Draw(IDXGISwapChain* pSwapChain)
		{
			// 现在準星走 3D 节点对齐（RenderHooks.cpp: AlignReticleToCamera），
			// 屏幕叠加会把准星画到镜筒外面，所以默认关闭。
			// 想对比两种效果时把它改成 true。
			static constexpr bool kScreenOverlay = false;
			if (!kScreenOverlay) return;

			if (!g_reticleVerifyADS) return;

			ID3D11Device* dev = nullptr;
			ID3D11DeviceContext* ctx = nullptr;
			if (FAILED(pSwapChain->GetDevice(__uuidof(ID3D11Device), (void**)&dev)) || !dev) return;
			dev->GetImmediateContext(&ctx);
			if (!ctx) { dev->Release(); return; }

			bool ok = true;
			do {
				if (!Init(dev)) { ok = false; break; }
				if (!EnsureBackBuffer(pSwapChain, dev)) { ok = false; break; }

				D3D11_TEXTURE2D_DESC td = {};
				s_bbTex->GetDesc(&td);

				// ---- 备份 ----
				ID3D11RenderTargetView* oldRTV = nullptr;
				ID3D11DepthStencilView* oldDSV = nullptr;
				ctx->OMGetRenderTargets(1, &oldRTV, &oldDSV);

				UINT nVp = D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE;
				D3D11_VIEWPORT oldVp[D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE] = {};
				ctx->RSGetViewports(&nVp, oldVp);

				ID3D11VertexShader* oldVS = nullptr;
				ID3D11PixelShader* oldPS = nullptr;
				ID3D11InputLayout* oldIL = nullptr;
				ID3D11Buffer* oldVB = nullptr;
				ID3D11Buffer* oldCBS[1] = {};
				ID3D11Buffer* oldPSCBS[1] = {};
				ID3D11ShaderResourceView* oldSRVs[2] = {};
				ID3D11SamplerState* oldSamps[1] = {};
				UINT oldStride = 0, oldOffset = 0;
				D3D11_PRIMITIVE_TOPOLOGY oldTopo = D3D11_PRIMITIVE_TOPOLOGY_UNDEFINED;
				ID3D11RasterizerState* oldRS = nullptr;
				ID3D11BlendState* oldBS = nullptr;
				FLOAT oldBlendFactor[4] = {};
				UINT oldSampleMask = 0;
				ID3D11DepthStencilState* oldDSS = nullptr;
				UINT oldStencilRef = 0;

				ctx->VSGetShader(&oldVS, nullptr, nullptr);
				ctx->PSGetShader(&oldPS, nullptr, nullptr);
				ctx->VSGetConstantBuffers(0, 1, oldCBS);
				ctx->PSGetConstantBuffers(0, 1, oldPSCBS);
				ctx->PSGetShaderResources(0, 2, oldSRVs);
				ctx->PSGetSamplers(0, 1, oldSamps);
				ctx->IAGetInputLayout(&oldIL);
				ctx->IAGetVertexBuffers(0, 1, &oldVB, &oldStride, &oldOffset);
				ctx->IAGetPrimitiveTopology(&oldTopo);
				ctx->RSGetState(&oldRS);
				ctx->OMGetBlendState(&oldBS, oldBlendFactor, &oldSampleMask);
				ctx->OMGetDepthStencilState(&oldDSS, &oldStencilRef);

				// ---- 状态设置 ----
				ID3D11RenderTargetView* rtvs[1] = { s_bbRTV };
				ctx->OMSetRenderTargets(1, rtvs, nullptr);

				D3D11_VIEWPORT vp = {};
				vp.TopLeftX = 0.0f;
				vp.TopLeftY = 0.0f;
				vp.Width = (FLOAT)td.Width;
				vp.Height = (FLOAT)td.Height;
				vp.MinDepth = 0.0f;
				vp.MaxDepth = 1.0f;
				ctx->RSSetViewports(1, &vp);
				ctx->RSSetState(s_rs);

				FLOAT blendFactor[4] = { 0.f, 0.f, 0.f, 0.f };
				ctx->OMSetBlendState(s_blend, blendFactor, 0xFFFFFFFF);
				ctx->OMSetDepthStencilState(s_ds, 0);

				ctx->IASetInputLayout(nullptr);
				ID3D11Buffer* noVB[1] = { nullptr };
				UINT zero = 0;
				ctx->IASetVertexBuffers(0, 1, noVB, &zero, &zero);
				ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);

				ReticleCB cbData = {};
				cbData.screenW = (float)td.Width;
				cbData.screenH = (float)td.Height;
				cbData.centerX = (float)td.Width * 0.5f;
				cbData.centerY = (float)td.Height * 0.5f;
				cbData.boxW = (float)td.Height * 0.55f;
				cbData.boxH = (float)td.Height * 0.55f;
				cbData.alphaMul = 1.0f;
				cbData.pad1 = 0.0f;

				D3D11_MAPPED_SUBRESOURCE ms = {};
				if (SUCCEEDED(ctx->Map(s_cb, 0, D3D11_MAP_WRITE_DISCARD, 0, &ms))) {
					memcpy(ms.pData, &cbData, sizeof(cbData));
					ctx->Unmap(s_cb, 0);
				}

				ID3D11Buffer* cbs[1] = { s_cb };
				ctx->VSSetConstantBuffers(0, 1, cbs);
				ctx->PSSetConstantBuffers(0, 1, cbs);
				ctx->VSSetShader(s_vs, nullptr, 0);
				ctx->PSSetShader(s_ps, nullptr, 0);
				ctx->PSSetShaderResources(0, 1, &s_srv);
				ctx->PSSetSamplers(0, 1, &s_sampler);

				ctx->Draw(4, 0);

				if (s_logCount < 5) {
					s_logCount++;
					Log("[ReticleBox] DRAW ok bb=%ux%u box=%.0f tex=%s oldRTV=%p oldDSV=%p oldVP=%.0fx%.0f",
						td.Width, td.Height, (float)cbData.boxW, s_texFromFile ? "DDS" : "GREEN",
						(void*)oldRTV, (void*)oldDSV,
						(FLOAT)oldVp[0].Width, (FLOAT)oldVp[0].Height);
				}

				// ---- 恢复 ----
				ctx->VSSetShader(oldVS, nullptr, 0);
				ctx->PSSetShader(oldPS, nullptr, 0);
				ctx->VSSetConstantBuffers(0, 1, oldCBS);
				ctx->PSSetConstantBuffers(0, 1, oldPSCBS);
				ID3D11ShaderResourceView* nullSRVs[2] = { nullptr, nullptr };
				ctx->PSSetShaderResources(0, 2, (oldSRVs[0] || oldSRVs[1]) ? oldSRVs : nullSRVs);
				if (oldSamps[0]) ctx->PSSetSamplers(0, 1, oldSamps);
				ctx->IASetInputLayout(oldIL);
				ctx->IASetVertexBuffers(0, 1, &oldVB, &oldStride, &oldOffset);
				ctx->IASetPrimitiveTopology(oldTopo);
				ctx->RSSetState(oldRS);
				ctx->OMSetBlendState(oldBS, oldBlendFactor, oldSampleMask);
				ctx->OMSetDepthStencilState(oldDSS, oldStencilRef);
				ctx->RSSetViewports(nVp, oldVp);
				ctx->OMSetRenderTargets(1, &oldRTV, oldDSV);

				if (oldVS) oldVS->Release();
				if (oldPS) oldPS->Release();
				if (oldCBS[0]) oldCBS[0]->Release();
				if (oldPSCBS[0]) oldPSCBS[0]->Release();
				if (oldSRVs[0]) oldSRVs[0]->Release();
				if (oldSRVs[1]) oldSRVs[1]->Release();
				if (oldSamps[0]) oldSamps[0]->Release();
				if (oldIL) oldIL->Release();
				if (oldVB) oldVB->Release();
				if (oldRS) oldRS->Release();
				if (oldBS) oldBS->Release();
				if (oldDSS) oldDSS->Release();
				if (oldRTV) oldRTV->Release();
				if (oldDSV) oldDSV->Release();
			} while (false);

			(void)ok;
			ctx->Release();
			dev->Release();
		}
	}

	HRESULT WINAPI D3DHooks::hkPresent(IDXGISwapChain* pSwapChain, UINT SyncInterval, UINT Flags)
	{
		// 递增帧计数器
		s_FrameNumber++;
		
		// 防止递归调用
		if (s_InPresent) {
			return s_OriginalPresent(pSwapChain, SyncInterval, Flags);
		}
#ifdef _DEBUG
		if (GetAsyncKeyState(VK_F3) & 1) {
			logger::info("Frame capture requested");
			if (rdoc_api)
				rdoc_api->TriggerCapture();
			else
				logger::error("rdoc_api is nullptr");
		}
#endif

		s_InPresent = true;
		HRESULT result = S_OK;

		if (imguiMgr && imguiMgr->IsInitialized()) {
			ImGuiContext* ctx = ImGui::GetCurrentContext();
			if (ctx != nullptr) {
				ImGuiIO& io = ImGui::GetIO();
				io.WantCaptureMouse = imguiMgr->IsMenuOpen();
			}
			imguiMgr->Update();
			imguiMgr->Render();
		}

		// [RETICLE 验证] 在 Present 之前直接绘制（不经过 ImGui，菜单关着也生效）
		ReticleBox::Draw(pSwapChain);

		// Call original Present
		result = s_OriginalPresent(pSwapChain, SyncInterval, Flags);

		s_InPresent = false;
		return result;
	}

	LRESULT CALLBACK D3DHooks::hkWndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
	{
		if (uMsg == WM_ACTIVATE) {
			WORD activateState = LOWORD(wParam);
			if (activateState == WA_INACTIVE) {
				if (imguiMgr && imguiMgr->IsMenuOpen()) {
					imguiMgr->ToggleMenu();
					imguiMgr->ForceHideCursor();
					RE::ControlMap::GetSingleton()->ignoreKeyboardMouse = false;
				}
				return CallWindowProc(s_OriginalWndProc, hWnd, uMsg, wParam, lParam);
			}
		}

		if (uMsg == WM_KILLFOCUS) {
			if (imguiMgr && imguiMgr->IsMenuOpen()) {
				imguiMgr->ToggleMenu();
				imguiMgr->ForceHideCursor();
				RE::ControlMap::GetSingleton()->ignoreKeyboardMouse = false;
			}
			return CallWindowProc(s_OriginalWndProc, hWnd, uMsg, wParam, lParam);
		}

		auto playerChar = RE::PlayerCharacter::GetSingleton();
		if (uMsg == WM_MOUSEWHEEL && s_EnableFOVAdjustment && playerChar && Utilities::IsInADS(playerChar)) {
			if (ScopeCamera::s_CurrentScopeNode) {
				short wheelDelta = GET_WHEEL_DELTA_WPARAM(wParam);
				ProcessMouseWheelFOVInput(wheelDelta);
				return true;
			}
		}

		if (imguiMgr && imguiMgr->IsInitialized() && imguiMgr->IsMenuOpen()) {
			if (GetForegroundWindow() == hWnd) {
				ImGuiContext* ctx = ImGui::GetCurrentContext();
				if (ctx != nullptr) {
					ImGui_ImplWin32_WndProcHandler(hWnd, uMsg, wParam, lParam);
				}
			}
		}

		return CallWindowProc(s_OriginalWndProc, hWnd, uMsg, wParam, lParam);
	}

	void WINAPI D3DHooks::hkRSSetViewports(ID3D11DeviceContext* pContext, UINT NumViewports, const D3D11_VIEWPORT* pViewports)
	{
		// 如果正在进行瞄具渲染，确保使用正确的全屏viewport
		// 这样可以防止其他MOD（如BakaFullscreenPipboy）的viewport修改影响瞄具渲染
		if (ScopeCamera::IsRenderingForScope() && NumViewports > 0 && pViewports != nullptr) {
			// 获取swap chain的后台缓冲区尺寸来确定正确的viewport
			Microsoft::WRL::ComPtr<ID3D11Device> device;
			pContext->GetDevice(&device);

			if (device && s_SwapChain) {
				Microsoft::WRL::ComPtr<ID3D11Texture2D> backBuffer;
				if (SUCCEEDED(s_SwapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&backBuffer))) {
					D3D11_TEXTURE2D_DESC desc;
					backBuffer->GetDesc(&desc);

					// 检查传入的viewport是否与预期的全屏viewport匹配
					// 如果不匹配（可能被其他MOD修改了），则强制使用正确的全屏viewport
					const D3D11_VIEWPORT& vp = pViewports[0];
					bool needsCorrection = false;

					// 检查viewport是否明显偏离全屏设置
					if (vp.Width < desc.Width * 0.9f || vp.Height < desc.Height * 0.9f ||
						vp.TopLeftX > desc.Width * 0.1f || vp.TopLeftY > desc.Height * 0.1f) {
						needsCorrection = true;
					}

					if (needsCorrection) {
						// 创建正确的全屏viewport
						D3D11_VIEWPORT fullViewport;
						fullViewport.TopLeftX = 0.0f;
						fullViewport.TopLeftY = 0.0f;
						fullViewport.Width = static_cast<float>(desc.Width);
						fullViewport.Height = static_cast<float>(desc.Height);
						fullViewport.MinDepth = 0.0f;
						fullViewport.MaxDepth = 1.0f;

						// 使用正确的全屏viewport
						return phookD3D11RSSetViewports(pContext, 1, &fullViewport);
					}
				}
			}
		}

		// 正常情况下透传调用
		return phookD3D11RSSetViewports(pContext, NumViewports, pViewports);
	}

	void D3DHooks::ProcessGamepadFOVInput()
	{
		// 先通过游戏的输入管理器检测手柄是否连接
		auto inputMgr = RE::BSInputDeviceManager::GetSingleton();
		if (!inputMgr || !inputMgr->IsGamepadConnected())
			return;
		
		DWORD64 currentTime = GetTickCount64();
		if (currentTime - s_LastGamepadInputTime < 100)  // 100ms防抖
			return;

		// 使用 XInput 读取手柄状态
		// Steam Input 会自动将 PS 控制器（DualShock 4、DualSense）映射为 XInput
		XINPUT_STATE state;
		ZeroMemory(&state, sizeof(XINPUT_STATE));

		if (XInputGetState(0, &state) != ERROR_SUCCESS)
			return;

		// D-pad 调整 FOV
		if (state.Gamepad.wButtons & XINPUT_GAMEPAD_DPAD_UP) {
			ScopeCamera::SetTargetFOV(ScopeCamera::GetTargetFOV() - s_FOVAdjustmentSensitivity * 2);
			s_LastGamepadInputTime = currentTime;
		} else if (state.Gamepad.wButtons & XINPUT_GAMEPAD_DPAD_DOWN) {
			ScopeCamera::SetTargetFOV(ScopeCamera::GetTargetFOV() + s_FOVAdjustmentSensitivity * 2);
			s_LastGamepadInputTime = currentTime;
		}
	}

	void D3DHooks::ProcessMouseWheelFOVInput(short wheelDelta)
	{
		auto playerChar = RE::PlayerCharacter::GetSingleton();

		if (!playerChar || !s_EnableFOVAdjustment || !Utilities::IsInADS(playerChar))
			return;

		// 滚轮向上为正值，向下为负值
		// 每个滚轮单位通常是120
		float fovDelta = (wheelDelta / 120.0f) * s_FOVAdjustmentSensitivity;
		ScopeCamera::SetTargetFOV(ScopeCamera::GetTargetFOV() - fovDelta);
	}

	void D3DHooks::HandleFOVInput()
	{
		auto playerChar = RE::PlayerCharacter::GetSingleton();
		if (!playerChar || !s_EnableFOVAdjustment || !Utilities::IsInADS(playerChar))
			return;

		// 处理手柄输入
		ProcessGamepadFOVInput();
	}

	BOOL __stdcall D3DHooks::ClipCursorHook(RECT* lpRect)
	{
		if (imguiMgr->IsMenuOpen()) {
			*lpRect = oldRect;
		}
		return phookClipCursor(lpRect);
	}

	void D3DHooks::CacheIAState(ID3D11DeviceContext* pContext)
	{
		// 缓存Input Layout
		pContext->IAGetInputLayout(s_CachedIAState.inputLayout.GetAddressOf());

		// 缓存Vertex Buffers
		pContext->IAGetVertexBuffers(0, IAStateCache::MAX_VERTEX_BUFFERS,
			reinterpret_cast<ID3D11Buffer**>(s_CachedIAState.vertexBuffers),
			s_CachedIAState.strides,
			s_CachedIAState.offsets);

		// 缓存Index Buffer
		pContext->IAGetIndexBuffer(s_CachedIAState.indexBuffer.GetAddressOf(),
			&s_CachedIAState.indexFormat,
			&s_CachedIAState.indexOffset);

		// 缓存Primitive Topology
		pContext->IAGetPrimitiveTopology(&s_CachedIAState.topology);
	}

	void D3DHooks::CacheVSState(ID3D11DeviceContext* pContext)
	{
		// 获取D3D11设备
		ID3D11Device* device = nullptr;
		pContext->GetDevice(&device);
		if (!device) {
			logger::error("Failed to get D3D11 device in CacheVSState");
			return;
		}

		// 缓存Vertex Shader
		ID3D11ClassInstance* classInstances[256];
		UINT numClassInstances = 256;
		pContext->VSGetShader(s_CachedVSState.vertexShader.GetAddressOf(),
			classInstances, &numClassInstances);

		// 释放class instances（如果有的话）
		for (UINT i = 0; i < numClassInstances; ++i) {
			if (classInstances[i]) {
				classInstances[i]->Release();
			}
		}

		// 缓存Constant Buffers - 获取原始指针并创建副本
		pContext->VSGetConstantBuffers(0, VSStateCache::MAX_CONSTANT_BUFFERS,
			reinterpret_cast<ID3D11Buffer**>(s_CachedVSState.constantBuffers));

		// 为每个绑定的常量缓冲区创建副本并复制数据
		for (UINT i = 0; i < VSStateCache::MAX_CONSTANT_BUFFERS; ++i) {
			if (s_CachedVSState.constantBuffers[i].Get()) {
				// 获取原始缓冲区描述
				D3D11_BUFFER_DESC originalDesc;
				s_CachedVSState.constantBuffers[i]->GetDesc(&originalDesc);

				// 创建可读的副本缓冲区
				D3D11_BUFFER_DESC copyDesc = originalDesc;
				copyDesc.Usage = D3D11_USAGE_DEFAULT;
				copyDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
				copyDesc.CPUAccessFlags = 0;
				copyDesc.MiscFlags = 0;

				HRESULT hr = device->CreateBuffer(&copyDesc, nullptr,
					s_CachedVSState.copiedConstantBuffers[i].GetAddressOf());

				if (SUCCEEDED(hr)) {
					// 复制缓冲区内容
					pContext->CopyResource(s_CachedVSState.copiedConstantBuffers[i].Get(),
						s_CachedVSState.constantBuffers[i].Get());
				} else {
					logger::error("Failed to create copy of constant buffer {}: 0x{:X}", i, hr);
					s_CachedVSState.copiedConstantBuffers[i].Reset();
				}
			}
		}

		// 缓存Shader Resources
		pContext->VSGetShaderResources(0, VSStateCache::MAX_SHADER_RESOURCES,
			reinterpret_cast<ID3D11ShaderResourceView**>(s_CachedVSState.shaderResources));

		// 缓存Samplers
		pContext->VSGetSamplers(0, VSStateCache::MAX_SAMPLERS,
			reinterpret_cast<ID3D11SamplerState**>(s_CachedVSState.samplers));

		// [Plan A] 从 cb2 提取 WVP 矩阵，计算 scope quad 屏幕中心位置
		// cb2 包含完整的 World-View-Projection 矩阵，用于将模型顶点变换到 clip space
		if (s_CachedVSState.copiedConstantBuffers[2].Get()) {
			// 创建 staging buffer 用于读取 cb2 数据
			D3D11_BUFFER_DESC stagingDesc;
			s_CachedVSState.copiedConstantBuffers[2]->GetDesc(&stagingDesc);
			stagingDesc.Usage = D3D11_USAGE_STAGING;
			stagingDesc.BindFlags = 0;
			stagingDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
			
			Microsoft::WRL::ComPtr<ID3D11Buffer> stagingBuffer;
			HRESULT hr = device->CreateBuffer(&stagingDesc, nullptr, stagingBuffer.GetAddressOf());
			if (SUCCEEDED(hr)) {
				pContext->CopyResource(stagingBuffer.Get(), s_CachedVSState.copiedConstantBuffers[2].Get());
				
				D3D11_MAPPED_SUBRESOURCE mapped;
				hr = pContext->Map(stagingBuffer.Get(), 0, D3D11_MAP_READ, 0, &mapped);
				if (SUCCEEDED(hr)) {
					// cb2 structure (WVP matrix): 4 float4 rows
					float* cb2Data = (float*)mapped.pData;
					// Row-major WVP matrix: cb2[0-3]
					float wvp00 = cb2Data[0], wvp01 = cb2Data[1], wvp02 = cb2Data[2], wvp03 = cb2Data[3];
					float wvp10 = cb2Data[4], wvp11 = cb2Data[5], wvp12 = cb2Data[6], wvp13 = cb2Data[7];
					float wvp20 = cb2Data[8], wvp21 = cb2Data[9], wvp22 = cb2Data[10], wvp23 = cb2Data[11];
					float wvp30 = cb2Data[12], wvp31 = cb2Data[13], wvp32 = cb2Data[14], wvp33 = cb2Data[15];
					
					pContext->Unmap(stagingBuffer.Get(), 0);
					
					// 计算模型原点 (0,0,0,1) 在 clip space 的位置
					// Shader: dp4 r1.x, cb2[0].xyzw, r0.xyzw  (r0 = vertex position, w=1)
					// 对于原点 (0,0,0,1): clipPos = cb2[row] * (0,0,0,1) = cb2[row].w
					float clipX = wvp03;  // cb2[0].w
					float clipY = wvp13;  // cb2[1].w
					float clipZ = wvp23;  // cb2[2].w
					float clipW = wvp33;  // cb2[3].w
					
					if (fabsf(clipW) > 0.0001f) {
						// 透视除法得到 NDC
						float ndcX = clipX / clipW;
						float ndcY = clipY / clipW;
						
						// 转换到 UV 空间 (0-1)
						// D3D NDC: X [-1,1] left-to-right, Y [-1,1] bottom-to-top
						float u = ndcX * 0.5f + 0.5f;
						float v = -ndcY * 0.5f + 0.5f;  // Y flip for D3D
						
						// 估算 scope quad 半径：使用模型空间 (+1,0,0,1) 点计算
						// 这假设 scope quad 模型在 X 方向有 1.0 单位的半径
						float edgeClipX = wvp00 + wvp03;  // (1,0,0,1) dot cb2[0]
						float edgeClipY = wvp10 + wvp13;  // (1,0,0,1) dot cb2[1]
						float edgeClipW = wvp30 + wvp33;  // (1,0,0,1) dot cb2[3]
						
						if (fabsf(edgeClipW) > 0.0001f) {
							float edgeNdcX = edgeClipX / edgeClipW;
							float edgeU = edgeNdcX * 0.5f + 0.5f;
							float radius = fabsf(edgeU - u);
							
							// 设置 scope quad 屏幕位置供 MV merge shader 使用
							RenderUtilities::SetScopeQuadScreenPosition(u, v, radius);
						}
					}
				}
			}
		}

		device->Release();
	}

	void D3DHooks::RestoreIAState(ID3D11DeviceContext* pContext)
	{
		if (!s_HasCachedState)
			return;

		// 恢复Input Layout
		pContext->IASetInputLayout(s_CachedIAState.inputLayout.Get());

		// 恢复Vertex Buffers
		ID3D11Buffer* vertexBuffers[IAStateCache::MAX_VERTEX_BUFFERS];
		for (int i = 0; i < IAStateCache::MAX_VERTEX_BUFFERS; ++i) {
			vertexBuffers[i] = s_CachedIAState.vertexBuffers[i].Get();
		}
		pContext->IASetVertexBuffers(0, IAStateCache::MAX_VERTEX_BUFFERS,
			vertexBuffers, s_CachedIAState.strides, s_CachedIAState.offsets);

		// 恢复Index Buffer
		pContext->IASetIndexBuffer(s_CachedIAState.indexBuffer.Get(),
			s_CachedIAState.indexFormat, s_CachedIAState.indexOffset);

		// 恢复Primitive Topology
		pContext->IASetPrimitiveTopology(s_CachedIAState.topology);
	}

	void D3DHooks::RestoreVSState(ID3D11DeviceContext* pContext)
	{
		if (!s_HasCachedState)
			return;

		// 恢复Vertex Shader
		pContext->VSSetShader(s_CachedVSState.vertexShader.Get(), nullptr, 0);

		// 恢复Constant Buffers - 使用复制的缓冲区
		ID3D11Buffer* constantBuffers[VSStateCache::MAX_CONSTANT_BUFFERS];
		for (int i = 0; i < VSStateCache::MAX_CONSTANT_BUFFERS; ++i) {
			// 优先使用复制的缓冲区，如果没有则使用原始指针
			if (s_CachedVSState.copiedConstantBuffers[i].Get()) {
				constantBuffers[i] = s_CachedVSState.copiedConstantBuffers[i].Get();
			} else {
				constantBuffers[i] = s_CachedVSState.constantBuffers[i].Get();
			}
		}
		pContext->VSSetConstantBuffers(0, VSStateCache::MAX_CONSTANT_BUFFERS, constantBuffers);

		// 恢复Shader Resources
		ID3D11ShaderResourceView* shaderResources[VSStateCache::MAX_SHADER_RESOURCES];
		for (int i = 0; i < VSStateCache::MAX_SHADER_RESOURCES; ++i) {
			shaderResources[i] = s_CachedVSState.shaderResources[i].Get();
		}
		pContext->VSSetShaderResources(0, VSStateCache::MAX_SHADER_RESOURCES, shaderResources);

		// 恢复Samplers
		ID3D11SamplerState* samplers[VSStateCache::MAX_SAMPLERS];
		for (int i = 0; i < VSStateCache::MAX_SAMPLERS; ++i) {
			samplers[i] = s_CachedVSState.samplers[i].Get();
		}
		pContext->VSSetSamplers(0, VSStateCache::MAX_SAMPLERS, samplers);
	}

	void D3DHooks::CacheRSState(ID3D11DeviceContext* pContext)
	{
		// 缓存Rasterizer State
		pContext->RSGetState(s_CachedRSState.rasterizerState.GetAddressOf());
	}

	void D3DHooks::CacheOMState(ID3D11DeviceContext* pContext)
	{
		// 缓存当前的RenderTarget和DepthStencil
		pContext->OMGetRenderTargets(
			OMStateCache::MAX_RENDER_TARGETS,
			reinterpret_cast<ID3D11RenderTargetView**>(s_CachedOMState.renderTargetViews),
			s_CachedOMState.depthStencilView.GetAddressOf()
		);

		s_CachedOMState.numRenderTargets = 0;
		for (UINT i = 0; i < OMStateCache::MAX_RENDER_TARGETS; ++i) {
			if (s_CachedOMState.renderTargetViews[i].Get()) {
				s_CachedOMState.numRenderTargets = i + 1;
			}
		}
	}

	void D3DHooks::RestoreRSState(ID3D11DeviceContext* pContext)
	{
		if (!s_HasCachedState)
			return;

		// 恢复Rasterizer State
		pContext->RSSetState(s_CachedRSState.rasterizerState.Get());
	}

	void D3DHooks::RestoreOMState(ID3D11DeviceContext* pContext)
	{
		//if (!s_HasCachedState)
		//	return;

		// 恢复所有缓存的RenderTarget
		ID3D11RenderTargetView* renderTargets[OMStateCache::MAX_RENDER_TARGETS];
		for (UINT i = 0; i < OMStateCache::MAX_RENDER_TARGETS; ++i) {
			renderTargets[i] = s_CachedOMState.renderTargetViews[i].Get();
		}

		pContext->OMSetRenderTargets(
			s_CachedOMState.numRenderTargets,
			renderTargets,
			s_CachedOMState.depthStencilView.Get()
		);
	}

	void D3DHooks::SetSecondRenderTargetAsActive()
	{
		if (!s_HasCachedState) {
			logger::error("No cached state available for SetSecondRenderTargetAsActive");
			return;
		}

		ID3D11RenderTargetView* targetRTV = nullptr;
		
		if (s_CachedOMState.numRenderTargets >= 2) {
			targetRTV = s_CachedOMState.renderTargetViews[1].Get();
		} else if (s_CachedOMState.numRenderTargets >= 1) {
			targetRTV = s_CachedOMState.renderTargetViews[0].Get();
		} else {
			logger::error("No render targets available in cached state");
			return;
		}

		// 设置单个RenderTarget，保持原有的DepthStencil
		m_Context->OMSetRenderTargets(1, &targetRTV, s_CachedOMState.depthStencilView.Get());
	}

	void D3DHooks::NameAllRenderTargets()
	{
		auto rendererData = RE::BSGraphics::RendererData::GetSingleton();
		if (!rendererData) {
			logger::warn("NameAllRenderTargets: RendererData not available");
			return;
		}

		// Render target names derived from independent IDA Pro analysis of Fallout 4 renderer
		// Based on shader inputs, string references, and rendering pipeline analysis
		static const char* rtNames[101] = {
			"RT_00_FrameBuffer",                     // 0 - Final SwapChain output
			"RT_01_RefractionNormals",               // 1 - Refraction normal map for water/glass
			"RT_02_ScenePreAlpha",                   // 2 - Scene before alpha blending
			"RT_03_SceneMain",                       // 3 - Main scene color buffer
			"RT_04_SceneTemp",                       // 4 - Scene temporary / PreUI buffer
			"RT_05_Unknown",                         // 5
			"RT_06_Unknown",                         // 6
			"RT_07_SSR_Raw",                         // 7 - Screen-Space Reflections raw data
			"RT_08_SSR_Blurred",                     // 8 - SSR blurred result
			"RT_09_SSR_BlurredExtra",                // 9 - SSR extra blur pass
			"RT_10_SSR_Direction",                   // 10 - SSR ray direction
			"RT_11_SSR_Mask",                        // 11 - SSR mask
			"RT_12_Unknown",                         // 12
			"RT_13_Unknown",                         // 13
			"RT_14_BlurVertical",                    // 14 - Vertical blur pass
			"RT_15_BlurHorizontal",                  // 15 - Horizontal blur (downscaled for color adjustment)
			"RT_16_Unknown",                         // 16
			"RT_17_UI",                              // 17 - User Interface
			"RT_18_UI_Temp",                         // 18 - UI temporary
			"RT_19_Unknown",                         // 19
			"RT_20_GBuffer_Normal",                  // 20 - G-Buffer normals
			"RT_21_GBuffer_NormalSwap",              // 21 - G-Buffer normals swap
			"RT_22_GBuffer_Albedo",                  // 22 - G-Buffer albedo/diffuse
			"RT_23_GBuffer_Emissive",                // 23 - G-Buffer emissive
			"RT_24_GBuffer_Material",                // 24 - G-Buffer material (Glossiness/Specular/SSS)
			"RT_25_Unknown",                         // 25
			"RT_26_TAA_History",                     // 26 - TAA accumulation history buffer
			"RT_27_TAA_HistorySwap",                 // 27 - TAA history swap buffer
			"RT_28_SSAO",                            // 28 - Screen-Space Ambient Occlusion
			"RT_29_MotionVectors",                   // 29 - TAA motion vectors
			"RT_30_Unknown",                         // 30
			"RT_31_Unknown",                         // 31
			"RT_32_Unknown",                         // 32
			"RT_33_Unknown",                         // 33
			"RT_34_Unknown",                         // 34
			"RT_35_Unknown",                         // 35
			"RT_36_UI_Downscaled",                   // 36 - Downscaled UI
			"RT_37_UI_DownscaledComposite",          // 37 - UI downscale composite
			"RT_38_Unknown",                         // 38
			"RT_39_DepthMips",                       // 39 - Main depth with mip levels
			"RT_40_Unknown",                         // 40
			"RT_41_Unknown",                         // 41
			"RT_42_Unknown",                         // 42
			"RT_43_Unknown",                         // 43
			"RT_44_Unknown",                         // 44
			"RT_45_Unknown",                         // 45
			"RT_46_Unknown",                         // 46
			"RT_47_Unknown",                         // 47
			"RT_48_SSAO_Temp1",                      // 48 - SSAO temporary buffer 1
			"RT_49_SSAO_Temp2",                      // 49 - SSAO temporary buffer 2
			"RT_50_SSAO_Temp3",                      // 50 - SSAO temporary buffer 3
			"RT_51_Unknown",                         // 51
			"RT_52_Unknown",                         // 52
			"RT_53_Unknown",                         // 53
			"RT_54_Unknown",                         // 54
			"RT_55_Unknown",                         // 55
			"RT_56_Unknown",                         // 56
			"RT_57_Mask",                            // 57 - Unknown mask
			"RT_58_DeferredDiffuse",                 // 58 - Deferred lighting diffuse
			"RT_59_DeferredSpecular",                // 59 - Deferred lighting specular
			"RT_60_Unknown",                         // 60
			"RT_61_Unknown",                         // 61
			"RT_62_Unknown",                         // 62
			"RT_63_Unknown",                         // 63
			"RT_64_HDR_Downscale",                   // 64 - HDR downscale base
			"RT_65_HDR_Luminance2",                  // 65 - HDR luminance level 2
			"RT_66_HDR_Luminance3",                  // 66 - HDR luminance level 3
			"RT_67_HDR_Luminance4",                  // 67 - HDR luminance level 4
			"RT_68_HDR_Adaptation",                  // 68 - HDR eye adaptation
			"RT_69_HDR_AdaptationSwap",              // 69 - HDR adaptation swap (1x1 pixel)
			"RT_70_HDR_Luminance6",                  // 70 - HDR luminance level 6
			"RT_71_Bloom1",                          // 71 - Bloom pass 1
			"RT_72_Bloom2",                          // 72 - Bloom pass 2
			"RT_73_Bloom3",                          // 73 - Bloom pass 3
			"RT_74_Unknown",                         // 74
			"RT_75_Unknown",                         // 75
			"RT_76_Unknown",                         // 76
			"RT_77_Unknown",                         // 77
			"RT_78_Unknown",                         // 78
			"RT_79_Unknown",                         // 79
			"RT_80_GodRays",                         // 80 - God rays / volumetric lighting
			"RT_81_VolumetricTemp",                  // 81 - Volumetric lighting temp
			"RT_82_Unknown",                         // 82
			"RT_83_Unknown",                         // 83
			"RT_84_Unknown",                         // 84
			"RT_85_Unknown",                         // 85
			"RT_86_Unknown",                         // 86
			"RT_87_Unknown",                         // 87
			"RT_88_Unknown",                         // 88
			"RT_89_Unknown",                         // 89
			"RT_90_Unknown",                         // 90
			"RT_91_Unknown",                         // 91
			"RT_92_Unknown",                         // 92
			"RT_93_Unknown",                         // 93
			"RT_94_Unknown",                         // 94
			"RT_95_Unknown",                         // 95
			"RT_96_Unknown",                         // 96
			"RT_97_Unknown",                         // 97
			"RT_98_Unknown",                         // 98
			"RT_99_Unknown",                         // 99
			"RT_100_Unknown"                         // 100
		};

		int namedCount = 0;
		for (int i = 0; i < 101; i++) {
			auto& rt = rendererData->renderTargets[i];
			
			// Name the texture
			if (rt.texture) {
				auto tex = reinterpret_cast<ID3D11Texture2D*>(rt.texture);
				tex->SetPrivateData(WKPDID_D3DDebugObjectName, 
					(UINT)strlen(rtNames[i]), rtNames[i]);
			}
			
			// Name the render target view
			if (rt.rtView) {
				auto rtv = reinterpret_cast<ID3D11RenderTargetView*>(rt.rtView);
				std::string rtvName = std::string(rtNames[i]) + "_RTV";
				rtv->SetPrivateData(WKPDID_D3DDebugObjectName,
					(UINT)rtvName.length(), rtvName.c_str());
			}
			
			// Name the shader resource view
			if (rt.srView) {
				auto srv = reinterpret_cast<ID3D11ShaderResourceView*>(rt.srView);
				std::string srvName = std::string(rtNames[i]) + "_SRV";
				srv->SetPrivateData(WKPDID_D3DDebugObjectName,
					(UINT)srvName.length(), srvName.c_str());
				namedCount++;
			}
		}

		logger::info("NameAllRenderTargets: Named {} render targets for RenderDoc debugging", namedCount);
	}

	void D3DHooks::CleanupStaticResources()
	{
		D3DResourceManager::GetSingleton()->Cleanup();
		


		logger::info("D3DHooks static resources cleaned up successfully");
	}
}
