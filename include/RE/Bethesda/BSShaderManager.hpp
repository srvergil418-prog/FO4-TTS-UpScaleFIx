#pragma once
// ============================================================================
// 兼容层：TTS 用旧式的 RE::BSShaderManager::GetDirectionalAmbientColorsA()，
// 但 powerof3/CommonLibF4 已经把这份数据重构到 Sky 类里了
// （见 Sky.h:176  NiColor directionalAmbientColorsA[3][2];  偏移 0x3B8）。
//
// 这里用 Sky 重新实现这两个旧接口，避免改动 TTS 的调用点。
// ============================================================================

#include <RE/Bethesda/Sky.h>

namespace RE
{
	class BSShaderManager
	{
	public:
		// 对应 Sky::directionalAmbientColorsA[3][2]
		[[nodiscard]] static NiColor (*GetDirectionalAmbientColorsA())[2]
		{
			auto* sky = Sky::GetSingleton();
			return sky ? sky->directionalAmbientColorsA : nullptr;
		}

		// powerof3 版本里没有对应的"local"数组，返回 nullptr。
		// TTS 的调用点都有 if (localColors) 判空，所以是安全的空操作。
		[[nodiscard]] static NiColor (*GetLocalDirectionalAmbientColorsA())[2]
		{
			return nullptr;
		}
	};
}
