#include "sceneloader.h"
#include "material.h"
#include "materialmanager.h"
#include "texturemanager.h"

void SceneLoader::parseUsdMaterials(
    const UsdStageRefPtr& stage, MaterialManager& materialManager,
    TextureManager& textureManager, const std::string& sceneDir,
    std::unordered_map<SdfPath, uint32_t, SdfPath::Hash>& materialMap) {
	std::cout << "Parsing USD materials..." << std::endl;

	// Phase 1: Collect all material info and texture paths
	std::vector<std::pair<SdfPath, MaterialTextureInfo>> materialInfos;
	std::vector<std::string> allTexturePaths;
	std::vector<bool> allTextureSRGB;

	auto addPath = [&](const std::string& path, bool sRGB) {
		if (!path.empty()) {
			allTexturePaths.push_back(path);
			allTextureSRGB.push_back(sRGB);
		}
	};

	for (const UsdPrim& prim : stage->Traverse()) {
		if (!prim.IsA<UsdShadeMaterial>()) {
			continue;
		}

		UsdShadeMaterial shadeMaterial(prim);
		UsdShadeShader surfaceShader = shadeMaterial.ComputeSurfaceSource();

		if (!surfaceShader) {
			continue;
		}

		MaterialTextureInfo info =
		    extractMaterialTextureInfo(surfaceShader, sceneDir);
		materialInfos.push_back({prim.GetPath(), info});

		addPath(info.albedoPath, true);
		addPath(info.normalPath, false);
		addPath(info.metallicPath, false);
		addPath(info.roughnessPath, false);
		addPath(info.aoPath, false);
		addPath(info.emissivePath, true);
	}

	// Phase 2: Batch load all textures
	std::vector<uint32_t> textureIDs =
	    textureManager.loadTexturesBatch(allTexturePaths, allTextureSRGB);

	// Build path->ID map for quick lookup
	std::unordered_map<std::string, uint32_t> pathToTextureID;
	for (size_t i = 0; i < allTexturePaths.size(); ++i) {
		pathToTextureID[allTexturePaths[i]] = textureIDs[i];
	}

	// Phase 3: Create materials with loaded textures
	for (const auto& [path, info] : materialInfos) {
		Material material;

		// Constants
		material.baseColor = info.baseColor;
		material.metallic = info.metallic;
		material.roughness = info.roughness;
		material.occlusion = info.occlusion;
		material.emissiveColor = info.emissiveColor;
		material.opacity = 1.0f - info.opacityThreshold;
		// TODO: better opacity system

		// Texture IDs
		auto resolveID = [&](const std::string& texPath) -> uint32_t {
			if (texPath.empty()) return 0;
			auto it = pathToTextureID.find(texPath);
			return (it != pathToTextureID.end()) ? it->second : 0;
		};

		material.albedoTextureID = resolveID(info.albedoPath);
		material.normalTextureID = resolveID(info.normalPath);
		material.metallicTextureID = resolveID(info.metallicPath);
		material.roughnessTextureID = resolveID(info.roughnessPath);
		material.aoTextureID = resolveID(info.aoPath);
		material.emissiveTextureID = resolveID(info.emissivePath);

		uint32_t materialID = materialManager.createMaterial(material);
		materialMap[path] = materialID;
	}

	std::cout << "Material parsing complete" << std::endl;
}

std::string SceneLoader::resolveInputTexturePath(const UsdShadeInput& input,
                                                 const std::string& sceneDir) {
	if (!input) {
		return "";
	}

	// Check if connected to something
	UsdShadeConnectableAPI source;
	TfToken sourceName;
	UsdShadeAttributeType sourceType;

	if (!input.GetConnectedSource(&source, &sourceName, &sourceType)) {
		// Not connected to anything
		return "";
	}

	UsdPrim sourcePrim = source.GetPrim();

	// Case 1: Connected to NodeGraph — walk through potentially nested graphs
	if (sourcePrim.IsA<UsdShadeNodeGraph>()) {
		UsdShadeConnectableAPI curSource = source;
		TfToken curSourceName = sourceName;

		while (curSource.GetPrim().IsA<UsdShadeNodeGraph>()) {
			UsdShadeNodeGraph nodeGraph(curSource.GetPrim());
			UsdShadeOutput output = nodeGraph.GetOutput(curSourceName);
			if (!output) return "";

			UsdShadeConnectableAPI innerSource;
			TfToken innerSourceName;
			UsdShadeAttributeType innerSourceType;
			if (!output.GetConnectedSource(&innerSource, &innerSourceName,
			                               &innerSourceType)) {
				return "";
			}
			curSource = innerSource;
			curSourceName = innerSourceName;
		}

		UsdShadeShader textureShader(curSource.GetPrim());
		if (textureShader) {
			return extractTextureFromShader(textureShader, sceneDir);
		}
	}
	// Case 2: Connected directly to shader (e.g., UsdUVTexture)
	else if (sourcePrim.IsA<UsdShadeShader>()) {
		UsdShadeShader textureShader(sourcePrim);
		return extractTextureFromShader(textureShader, sceneDir);
	}

	return "";
}

std::string SceneLoader::extractTextureFromShader(const UsdShadeShader& shader,
                                                  const std::string& sceneDir) {
	UsdShadeInput fileInput = shader.GetInput(TfToken("file"));
	if (!fileInput) return "";

	SdfAssetPath assetPath;
	if (!fileInput.Get(&assetPath)) return "";

	std::string path = assetPath.GetResolvedPath();
	if (path.empty()) path = assetPath.GetAssetPath();
	if (path.empty()) return "";

	namespace fs = std::filesystem;
	auto tryPath = [](const std::string& p) -> std::string {
		return fs::exists(p) ? p : "";
	};

	if (auto r = tryPath(path); !r.empty()) return r;
	if (auto r = tryPath(sceneDir + path); !r.empty()) return r;

	size_t slash = path.find_last_of("/\\");
	if (slash != std::string::npos) {
		if (auto r = tryPath(sceneDir + path.substr(slash + 1)); !r.empty())
			return r;
	}

	std::cerr << "Could not resolve texture path: " << assetPath << "\n";
	return "";
}

MaterialTextureInfo SceneLoader::extractMaterialTextureInfo(
    const UsdShadeShader& shader, const std::string& sceneDir) {
	MaterialTextureInfo info;

	// Only handle UsdPreviewSurface
	TfToken shaderId;
	shader.GetIdAttr().Get(&shaderId);

	if (shaderId != TfToken("UsdPreviewSurface")) {
		return info;
	}

	// diffuse color (rename to albedo when processed)
	UsdShadeInput diffuseInput = shader.GetInput(TfToken("diffuseColor"));
	if (diffuseInput) {
		GfVec3f colorValue;
		if (diffuseInput.Get(&colorValue)) {
			info.baseColor =
			    glm::vec3(colorValue[0], colorValue[1], colorValue[2]);
		}

		std::string diffusePath =
		    resolveInputTexturePath(diffuseInput, sceneDir);
		if (!diffusePath.empty()) {
			info.albedoPath = diffusePath;
		}
	}

	// normal (we don't use a fallback here - default normal in shader)
	UsdShadeInput normalInput = shader.GetInput(TfToken("normal"));
	if (normalInput) {
		std::string texturePath =
		    resolveInputTexturePath(normalInput, sceneDir);
		if (!texturePath.empty()) {
			info.normalPath = texturePath;
		}
	}

	// metallic
	UsdShadeInput metallicInput = shader.GetInput(TfToken("metallic"));
	if (metallicInput) {
		float metallicValue;
		if (metallicInput.Get(&metallicValue)) {
			info.metallic = metallicValue;
		}

		std::string metallicPath =
		    resolveInputTexturePath(metallicInput, sceneDir);
		if (!metallicPath.empty()) {
			info.metallicPath = metallicPath;
		}
	}

	// roughness
	UsdShadeInput roughnessInput = shader.GetInput(TfToken("roughness"));
	if (roughnessInput) {
		float roughnessValue;
		if (roughnessInput.Get(&roughnessValue)) {
			info.roughness = roughnessValue;
		}

		std::string roughnessPath =
		    resolveInputTexturePath(roughnessInput, sceneDir);
		if (!roughnessPath.empty()) {
			info.roughnessPath = roughnessPath;
		}
	}

	// occlusion (ao rename)
	UsdShadeInput occlusionInput = shader.GetInput(TfToken("occlusion"));
	if (occlusionInput) {
		float occlusionValue = 1.0f;
		if (occlusionInput.Get(&occlusionValue)) {
			info.occlusion = occlusionValue;
		}

		std::string occlusionPath =
		    resolveInputTexturePath(occlusionInput, sceneDir);
		if (!occlusionPath.empty()) {
			info.aoPath = occlusionPath;
		}
	}

	// emissive
	UsdShadeInput emissiveInput = shader.GetInput(TfToken("emissiveColor"));
	if (emissiveInput) {
		GfVec3f emissiveValue;
		if (emissiveInput.Get(&emissiveValue)) {
			info.emissiveColor =
			    glm::vec3(emissiveValue[0], emissiveValue[1], emissiveValue[2]);
		}

		std::string emissivePath =
		    resolveInputTexturePath(emissiveInput, sceneDir);
		if (!emissivePath.empty()) {
			info.emissivePath = emissivePath;
		}
	}

	UsdShadeInput opacityThresholdInput =
	    shader.GetInput(TfToken("opacityThreshold"));
	if (opacityThresholdInput) {
		float value = 0.0f;
		if (opacityThresholdInput.Get(&value)) {
			info.opacityThreshold = value;
		}
	}

	// TODO:
	UsdShadeInput opacityInput = shader.GetInput(TfToken("opacity"));
	if (opacityInput) {
		std::string path = resolveInputTexturePath(opacityInput, sceneDir);
		if (!path.empty()) info.opacityPath = path;
	}

	return info;
}
