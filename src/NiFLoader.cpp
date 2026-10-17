#include "NiFLoader.h"

#include <RE/Bethesda/BSModelDB.h>
#include <Windows.h>
#include <cstdio>

using namespace RE;

NIFLoader* NIFLoader::GetSingleton()
{
	static NIFLoader instance;
	return &instance;
}

// ===========================================================================
// 零风险 ID 探测（只读，不调用被探测的函数）
//
// 为什么需要：Fallout4.exe 的 .text 是加壳/加密的（实测熵 7.998 bits/byte，
// 正常代码约 6.3），无法静态反汇编确认 REL::ID(999778) / (678544) 指向什么。
// 直接调用那版的后果是启动即崩，说明 ID 或调用方式有问题。
//
// 这里只读三样东西：
//   1. REL::Relocation 解析出的地址；
//   2. VirtualQuery 的内存属性（是否可执行 → 是代码还是数据）；
//   3. 地址 - 模块基址，与地址库里记录的 offset 对照。
//
// 结果写到固定文件，不依赖 spdlog。
// ===========================================================================
namespace
{
	void ProbeRelocations()
	{
		static bool s_done = false;
		if (s_done) {
			return;
		}
		s_done = true;

		HANDLE h = ::CreateFileA("E:\\Custom Fallout4\\tts-probe.txt",
			GENERIC_WRITE, FILE_SHARE_READ, nullptr,
			CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
		if (h == INVALID_HANDLE_VALUE) {
			return;
		}

		auto write = [h](const char* a_text) {
			DWORD written = 0;
			::WriteFile(h, a_text, (DWORD)strlen(a_text), &written, nullptr);
		};

		char buf[640];
		const std::uintptr_t exeBase = reinterpret_cast<std::uintptr_t>(::GetModuleHandleA(nullptr));
		sprintf_s(buf,
			"=== TTS REL::ID probe (read-only) ===\n"
			"Fallout4 base = 0x%llX\n\n",
			(unsigned long long)exeBase);
		write(buf);

		auto probe = [&](const char* label, std::uint64_t libOffset, std::uintptr_t addr) {
			MEMORY_BASIC_INFORMATION mbi{};
			const SIZE_T q = ::VirtualQuery(reinterpret_cast<LPCVOID>(addr), &mbi, sizeof(mbi));

			const bool exec = (q == sizeof(mbi)) &&
			                  (mbi.State == MEM_COMMIT) &&
			                  (mbi.Protect & (PAGE_EXECUTE | PAGE_EXECUTE_READ |
			                                  PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY));

			sprintf_s(buf,
				"%s\n  addr=0x%llX  baseOff=0x%llX  libOffset=0x%llX  offsetMatch=%s\n"
				"  VirtualQueryRet=%llu  State=0x%X  Protect=0x%X  Type=0x%X  allocBase=0x%llX\n"
				"  => %s\n\n",
				label,
				(unsigned long long)addr,
				(unsigned long long)(addr - exeBase),
				(unsigned long long)libOffset,
				((addr - exeBase) == libOffset) ? "YES" : "no",
				(unsigned long long)q,
				q == sizeof(mbi) ? mbi.State : 0u,
				q == sizeof(mbi) ? mbi.Protect : 0u,
				q == sizeof(mbi) ? mbi.Type : 0u,
				q == sizeof(mbi) ? (unsigned long long)mbi.BaseAddress : 0ull,
				exec ? "EXECUTABLE -> points at code" : "NOT executable -> data or invalid");
			write(buf);
		};

		// 已知应该指向代码的对照项：TTS 一直用它做 DrawTriShape 相关调用。
		{ static REL::Relocation<std::uintptr_t> r{ REL::ID(763320) }; probe("CONTROL: ID 763320 (known in use for DrawTriShape)", 0x1d0bfb0, r.address()); }
		// 待确认的两个。
		{ static REL::Relocation<std::uintptr_t> r{ REL::ID(999778) }; probe("CANDIDATE: ID 999778 (guess: BSStream::Create)", 0x549550, r.address()); }
		{ static REL::Relocation<std::uintptr_t> r{ REL::ID(678544) }; probe("CANDIDATE: ID 678544 (guess: BSStream::Load)", 0x1cdfee0, r.address()); }
		// 作者 DLL 里 hook 的另一个 Load 重载。
		{ static REL::Relocation<std::uintptr_t> r{ REL::ID(160035) }; probe("REFERENCE: ID 160035 (TTS hooks this as BSStream::Load 3-arg)", 0x1cdfe20, r.address()); }

		write("=== probe done ===\n");
		::CloseHandle(h);
	}

	// -----------------------------------------------------------------------
	// 加载路径：仍然用 BSModelDB（可用），直到 BSStream 的 ID 被证实。
	// 注意：Demand 返回模型缓存里的共享实例 —— 这正是"切枪后 TTSNode 异常"
	// 的疑点，但把它换掉的尝试（r170 直接调 BSStream）会崩，所以暂时保留，
	// 等探测结果出来再决定。
	// -----------------------------------------------------------------------
	RE::NiNode* LoadModelViaModelDB(const char* a_path)
	{
		RE::NiPointer<RE::NiNode> model;
		RE::BSModelDB::DBTraits::ArgsType args{};
		args.prepareAfterLoad = 1;
		args.loadTextures = 1;
		args.performProcess = 1;
		(void)RE::BSModelDB::Demand(a_path, &model, args);

		RE::NiNode* node = model.get();
		if (node) {
			// 接管一份引用，避免局部的 NiPointer 析构后把节点一起销毁。
			node->IncRefCount();
		}
		return node;
	}
}

NiNode* NIFLoader::LoadNIF(const char* filePath)
{
	ProbeRelocations();

	BSStream* stream = BSStream::Create();
	if (!stream) {
		logger::error("Failed to create BSStream for: {}", filePath);
		return nullptr;
	}
	if (!stream->Load(filePath)) {
		logger::error("Failed to load NIF file via BSStream: {}", filePath);
		return nullptr;
	}
	NiNode* rootNode = stream->GetTopLevelNode();
	if (!rootNode) {
		logger::error("Failed to get root NiNode: {}", filePath);
		return nullptr;
	}
	return rootNode;
}

bool NIFLoader::LoadNIFAndAttach(const char* filePath)
{
	NiNode* rootNode = LoadNIF(filePath);
	if (!rootNode) {
		return false;
	}

	auto* pc = PlayerCharacter::GetSingleton();
	if (!pc || !pc->Get3D()) {
		logger::error("LoadNIFAndAttach: no player 3D");
		return false;
	}
	auto* weaponObj = pc->Get3D()->GetObjectByName("Weapon");
	auto* weaponnode = weaponObj ? weaponObj->IsNode() : nullptr;
	if (!weaponnode) {
		logger::error("LoadNIFAndAttach: no Weapon node");
		return false;
	}

	weaponnode->AttachChild(rootNode, false);
	rootNode->local.translate = NiPoint3(0, 0, 10);
	rootNode->local.rotate.MakeIdentity();

	NiUpdateData udata;
	weaponnode->Update(udata);
	rootNode->Update(udata);

	return true;
}

void NIFLoader::ProcessNiNode(NiNode* node)
{
	if (!node) {
		return;
	}

	for (uint16_t i = 0; i < node->children.size(); ++i) {
		NiPointer<NiAVObject> childObject = node->children[i];
		if (childObject) {
			BSTriShape* triShape = childObject->IsTriShape();
			if (triShape) {
				ProcessBSTriShape(triShape);
			} else {
				NiNode* childNode = childObject->IsNode();
				if (childNode) {
					ProcessNiNode(childNode);
				}
			}
		}
	}
}

void NIFLoader::ProcessBSTriShape(BSTriShape* triShape)
{
	if (!triShape) {
		return;
	}

	// QShaderProperty() is unavailable in powerof3 -- the build shim replaces
	// this line (see ci/build.ps1 step 8). Keep the statement shape intact.
	NiShadeProperty* shadeProperty = triShape->QShaderProperty();
	if (shadeProperty) {
		BSEffectShaderProperty* effectShader = nullptr;
		if (shadeProperty->RTTI.id() == RTTI::BSEffectShaderProperty.id()) {
			effectShader = static_cast<BSEffectShaderProperty*>(shadeProperty);
			ProcessBSEffectShaderProperty(effectShader);
		}
	}

	NiAlphaProperty* alphaProperty = (NiAlphaProperty*)triShape->properties[1].get();
	if (alphaProperty) {
		ProcessNiAlphaProperty(alphaProperty);
	}
}

void NIFLoader::ProcessBSEffectShaderProperty(BSEffectShaderProperty* property)
{
	if (!property) {
		return;
	}
}

void NIFLoader::ProcessNiAlphaProperty(NiAlphaProperty* property)
{
	if (!property) {
		return;
	}
}
