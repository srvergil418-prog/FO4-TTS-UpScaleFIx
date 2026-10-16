#include "NiFLoader.h"

#include <RE/Bethesda/BSModelDB.h>   // BSModelDB::Demand -- the only working model loader we have

using namespace RE;

namespace
{
	// Load a NIF through the game's own model database.
	//
	// WHY THIS REPLACES BSStream:
	//   NIFLoader used to do `BSStream::Create()` + `stream->Load(path)`. Both are
	//   engine functions that live in Fallout4.exe and are NOT exported by
	//   CommonLibF4.lib (powerof3 compiles no NiStream.cpp), so the build shim in
	//   ci/PCH.append.h had to fake them -- with `Load()` simply returning false.
	//   Consequence: NIFLoader::LoadNIF ALWAYS returned nullptr, the scope model
	//   never attached, and the in-game settings panel showed exactly
	//       "无 TTSNode"  and  "状态: 未加载"
	//   while aiming produced no visible effect at all.
	//
	//   BSModelDB::Demand(const char*, NiPointer<NiNode>*, const ArgsType&) is the
	//   game's supported "load this model by path" entry point, and both of its
	//   REL::IDs (1066398 / 1225688) resolve fine against the 1.10.163 address
	//   library -- master and dev-1.10.163 even agree on the values.
	//
	// CAVEAT: Demand hands back a handle into the shared model cache, so callers
	// must not mutate the returned node's transform/properties in place. The
	// attach path below is the one place that could, and it is only reached for
	// the optional custom-scope-model feature.
	RE::NiNode* LoadModelViaModelDB(const char* a_path)
	{
		RE::NiPointer<RE::NiNode> model;
		RE::BSModelDB::DBTraits::ArgsType args{};
		args.prepareAfterLoad = 1;
		args.loadTextures = 1;
		args.performProcess = 1;
		(void)RE::BSModelDB::Demand(a_path, &model, args);
		return model.get();
	}
}

NIFLoader* NIFLoader::GetSingleton()
{
	static NIFLoader instance;
	return &instance;
}

NiNode* NIFLoader::LoadNIF(const char* filePath)
{
	NiNode* rootNode = LoadModelViaModelDB(filePath);
	if (!rootNode) {
		logger::error("Failed to load NIF file: {}", filePath);
		return nullptr;
	}
	return rootNode;
}

bool NIFLoader::LoadNIFAndAttach(const char* filePath)
{
	NiNode* rootNode = LoadModelViaModelDB(filePath);
	if (!rootNode) {
		logger::error("Failed to load NIF file: {}", filePath);
		return false;
	}

	auto weaponnode = PlayerCharacter::GetSingleton()->Get3D(true)->GetObjectByName("Weapon")->IsNode();
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



	// Iterate through the children of the NiNode
	for (uint16_t i = 0; i < node->children.size(); ++i) {
		NiPointer<NiAVObject> childObject = node->children[i];
		if (childObject) {
			// Check if the child is a BSTriShape
			BSTriShape* triShape = childObject->IsTriShape();
			if (triShape) {
				ProcessBSTriShape(triShape);
			} else {
				// If it's another NiNode, process recursively
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



	// Get the vertex and index data
	BSGraphics::VertexDesc vertexDesc = triShape->vertexDesc;


	// Get the shader property
	NiShadeProperty* shadeProperty = triShape->QShaderProperty();
	if (shadeProperty) {
		// Try to cast to BSEffectShaderProperty
		BSEffectShaderProperty* effectShader = nullptr;
		if (shadeProperty->RTTI.id() == RTTI::BSEffectShaderProperty.id()) {
			effectShader = static_cast<BSEffectShaderProperty*>(shadeProperty);
			ProcessBSEffectShaderProperty(effectShader);
		}
	} else {

	}

	// Get the alpha property
	NiAlphaProperty* alphaProperty = (NiAlphaProperty*)triShape->properties[1].get();
	if (alphaProperty) {
		ProcessNiAlphaProperty(alphaProperty);
	} else {

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
