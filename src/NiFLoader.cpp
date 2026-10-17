#include "NiFLoader.h"

using namespace RE;

NIFLoader* NIFLoader::GetSingleton()
{
	static NIFLoader instance;
	return &instance;
}

// ---------------------------------------------------------------------------
// NIF loading -- restored to the AUTHOR'S original approach.
//
// History: this file was rewritten to use BSModelDB::Demand because the build
// shim's fake BSStream always returned false. But Demand hands back a SHARED
// instance from the model cache, so attaching it to a weapon and detaching it
// again disturbed every other user of that model. That is exactly why TTSNode
// misbehaved after weapon swaps in our build while the author's build was fine
// (user-confirmed): the node was never really "ours".
//
// BSStream is engine code, not something the author owns -- his IDs are plain
// immediates inside his DLL, recovered from the machine code:
//     mov ecx, 0x708                 ; sizeof(BSStream)
//     mov qword [rbp+7], 0x0F4162    ; REL::ID(999778) = BSStream::Create
//     mov qword [rbp+7], 0x0A5A90    ; REL::ID(678544) = BSStream::Load(const char*)
// Both IDs resolve in version-1-10-163-0.bin. See ci/PCH.append.h for the
// implementation; GetTopLevelNode() is topObjects[0] (NiStream+0x358, _data at
// +0x08, elements are NiPointer<NiObject> = one NiObject* per slot).
// ---------------------------------------------------------------------------
NiNode* NIFLoader::LoadNIF(const char* filePath)
{
	BSStream* stream = BSStream::Create();
	if (!stream) {
		logger::error("Failed to create BSStream for: {}", filePath);
		return nullptr;
	}

	if (!stream->Load(filePath)) {
		logger::error("Failed to load NIF file: {}", filePath);
		return nullptr;
	}

	NiNode* rootNode = stream->GetTopLevelNode();
	if (!rootNode) {
		logger::error("Failed to get root NiNode from NIF file: {}", filePath);
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
