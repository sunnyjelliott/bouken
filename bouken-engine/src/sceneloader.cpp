#include "sceneloader.h"
#include "material.h"
#include "materialmanager.h"
#include "primitives.h"
#include "rendersystem.h"
#include "texturemanager.h"
#include "transform.h"

#include "mikktspace.h"

bool SceneLoader::loadScene(const std::string& filepath, World& world,
                            RenderSystem& renderSystem,
                            TextureManager& textureManager,
                            MaterialManager& materialManager,
                            const SceneLoadOptions& options) {
	SceneFormat format = options.format;
	if (format == SceneFormat::AUTO) {
		format = detectFormat(filepath);
	}

	switch (format) {
		case SceneFormat::USD:
			return loadUSD(filepath, world, renderSystem, textureManager,
			               materialManager, options);
		case SceneFormat::OBJ:
			return loadOBJ(filepath, world, renderSystem, textureManager,
			               materialManager, options);
		default:
			std::cerr << "Unsupported scene file format" << filepath
			          << std::endl;
			return false;
	}
}

SceneFormat SceneLoader::detectFormat(const std::string& filepath) {
	size_t extPos = filepath.find_last_of('.');
	if (extPos == std::string::npos) {
		return SceneFormat::AUTO;
	}

	std::string ext = filepath.substr(extPos);
	std::transform(ext.begin(), ext.end(), ext.begin(),
	               [](unsigned char c) { return std::tolower(c); });

	if (ext == ".usd" || ext == ".usda" || ext == ".usdc") {
		return SceneFormat::USD;
	} else if (ext == ".obj") {
		return SceneFormat::OBJ;
	}

	return SceneFormat::AUTO;
}

bool SceneLoader::loadUSD(const std::string& filepath, World& world,
                          RenderSystem& renderSystem,
                          TextureManager& textureManager,
                          MaterialManager& materialManager,
                          const SceneLoadOptions& options) {
	std::cout << "Loading USD scene: " << filepath << std::endl;
	std::filesystem::path absolutepath = std::filesystem::absolute(filepath);
	UsdStageRefPtr stage = UsdStage::Open(absolutepath.generic_string());
	if (!stage) {
		std::cerr << "Failed to open USD stage: " << filepath << std::endl;
		return false;
	}

	// Get root prim
	UsdPrim rootPrim = stage->GetPseudoRoot();
	if (!rootPrim) {
		std::cerr << "Root prim error" << std::endl;
		return false;
	}

	size_t lastSlash = filepath.find_last_of("/\\");
	std::string sceneDir = (lastSlash != std::string::npos)
	                           ? filepath.substr(0, lastSlash + 1)
	                           : "";

	// PASS 1: Parse all Material prims, create materials
	std::unordered_map<SdfPath, uint32_t, SdfPath::Hash> materialMap;
	parseUsdMaterials(stage, materialManager, textureManager, sceneDir,
	                  materialMap);

	// PASS 2: Traverse geometry, bind materials
	UsdGeomXformCache xformCache;
	for (const UsdPrim& child : rootPrim.GetChildren()) {
		traverseUsdPrim(child, world, renderSystem, materialMap,
		                options.parentEntity, xformCache);
	}
	renderSystem.flushMeshUploads();

	std::cout << "USD scene loaded successfully" << std::endl;
	std::cout << "  Materials: " << materialManager.getMaterialCount()
	          << std::endl;
	std::cout << "  Entities: " << world.getEntityCount() << std::endl;

	return true;
}

bool SceneLoader::loadOBJ(const std::string& filepath, World& world,
                          RenderSystem& renderSystem,
                          TextureManager& textureManager,
                          MaterialManager& materialManager,
                          const SceneLoadOptions& options) {
	std::cout << "Loading OBJ: " << filepath << std::endl;

	std::cerr << "OBJ loading not yet implemented in SceneLoader" << std::endl;
	return false;
}

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

namespace {

struct UsdMeshData {
	VtArray<GfVec3f> points;
	VtArray<GfVec3f> normals;
	bool hasNormals = false;
	TfToken normalInterpolation;
	VtArray<GfVec2f> uvs;
	bool hasUVs = false;
	TfToken uvInterpolation;
	VtArray<GfVec3f> tangents;
	VtArray<float> tangentSigns;
	bool hasTangents = false;
	VtArray<int> faceVertexCounts;
	VtArray<int> faceVertexIndices;
	TfToken tangentInterpolation;
};

// Converts a GfMatrix4d to glm::mat4. USD uses row-vector convention
// (v' = v*M) and GLM uses column-vector convention (v' = M*v), but the
// memory layout for the same geometric transformation is identical — each
// convention stores "image of basis i" as the i-th block of 4 floats. A
// direct element copy (result[i][j] = m[i][j]) is correct; do NOT transpose.
static glm::mat4 gfMatrixToGlm(const GfMatrix4d& m) {
	glm::mat4 result;
	for (int i = 0; i < 4; ++i)
		for (int j = 0; j < 4; ++j) result[i][j] = static_cast<float>(m[i][j]);
	return result;
}

// Bakes a world-space transform into every vertex's position, normal, and
// tangent. Call after MikkTSpace + triangulateFaces, before uploadMesh.
static void applyWorldTransform(std::vector<Vertex>& vertices,
                                const glm::mat4& worldMat) {
	const glm::mat3 normalMat =
	    glm::transpose(glm::inverse(glm::mat3(worldMat)));
	for (Vertex& v : vertices) {
		v.position = glm::vec3(worldMat * glm::vec4(v.position, 1.0f));
		const glm::vec3 tn = normalMat * v.normal;
		v.normal = (glm::dot(tn, tn) > 1e-10f) ? glm::normalize(tn)
		                                       : glm::vec3(0.0f, 1.0f, 0.0f);
		const glm::vec3 tt = glm::mat3(worldMat) * glm::vec3(v.tangent);
		v.tangent = glm::vec4(glm::normalize(tt), v.tangent.w);
	}
}

// -----------------------------------------------------------------------
// MikkTSpace tangent generation
// Works on original USD face data (n-gons) so MikkTSpace can use its
// native quad handling and QUAD_ONE_DEGEN_TRI optimisation.
// -----------------------------------------------------------------------
struct MikkTSpaceContext {
	const UsdMeshData* data;
	const VtArray<int>* faceIndices;  // subset (or all) faces
	std::vector<size_t>
	    faceStartOffsets;  // global offset per face into faceVertexIndices
	std::vector<size_t>
	    subsetFaceVertexOffsets;  // cumulative fv-count within subset
	std::vector<glm::vec4>
	    tangents;  // output; size == subsetFaceVertexOffsets.back()
};

static int mikkGetNumFaces(const SMikkTSpaceContext* ctx) {
	auto* d = static_cast<MikkTSpaceContext*>(ctx->m_pUserData);
	return static_cast<int>(d->faceIndices->size());
}

static int mikkGetNumVerticesOfFace(const SMikkTSpaceContext* ctx, int face) {
	auto* d = static_cast<MikkTSpaceContext*>(ctx->m_pUserData);
	int globalFaceIdx = (*d->faceIndices)[face];
	return d->data->faceVertexCounts[globalFaceIdx];
}

static void mikkGetPosition(const SMikkTSpaceContext* ctx, float outPos[3],
                            int face, int vert) {
	auto* d = static_cast<MikkTSpaceContext*>(ctx->m_pUserData);
	int globalFvIdx =
	    static_cast<int>(d->faceStartOffsets[(*d->faceIndices)[face]]) + vert;
	int pointIdx = d->data->faceVertexIndices[globalFvIdx];
	const GfVec3f& p = d->data->points[pointIdx];
	outPos[0] = p[0];
	outPos[1] = p[1];
	outPos[2] = p[2];
}

static void mikkGetNormal(const SMikkTSpaceContext* ctx, float outNorm[3],
                          int face, int vert) {
	auto* d = static_cast<MikkTSpaceContext*>(ctx->m_pUserData);
	int globalFvIdx =
	    static_cast<int>(d->faceStartOffsets[(*d->faceIndices)[face]]) + vert;
	int pointIdx = d->data->faceVertexIndices[globalFvIdx];
	int normalIdx;
	if (d->data->normalInterpolation == UsdGeomTokens->faceVarying)
		normalIdx = globalFvIdx;
	else if (d->data->normalInterpolation == UsdGeomTokens->uniform)
		normalIdx = (*d->faceIndices)[face];
	else
		normalIdx = pointIdx;
	glm::vec3 n(0.0f, 1.0f, 0.0f);
	if (d->data->hasNormals && normalIdx < (int)d->data->normals.size()) {
		n = glm::normalize(glm::vec3(d->data->normals[normalIdx][0],
		                             d->data->normals[normalIdx][1],
		                             d->data->normals[normalIdx][2]));
	}
	outNorm[0] = n.x;
	outNorm[1] = n.y;
	outNorm[2] = n.z;
}

static void mikkGetTexCoord(const SMikkTSpaceContext* ctx, float outUV[2],
                            int face, int vert) {
	auto* d = static_cast<MikkTSpaceContext*>(ctx->m_pUserData);
	int globalFvIdx =
	    static_cast<int>(d->faceStartOffsets[(*d->faceIndices)[face]]) + vert;
	int pointIdx = d->data->faceVertexIndices[globalFvIdx];
	int uvIdx = (d->data->uvInterpolation == UsdGeomTokens->faceVarying)
	                ? globalFvIdx
	                : pointIdx;
	if (d->data->hasUVs && uvIdx < (int)d->data->uvs.size()) {
		outUV[0] = d->data->uvs[uvIdx][0];
		outUV[1] =
		    1.0f - d->data->uvs[uvIdx][1];  // flip V to match shader UV space
	} else {
		outUV[0] = outUV[1] = 0.0f;
	}
}

static void mikkSetTSpaceBasic(const SMikkTSpaceContext* ctx,
                               const float tangent[3], float sign, int face,
                               int vert) {
	auto* d = static_cast<MikkTSpaceContext*>(ctx->m_pUserData);
	size_t subsetFvIdx = d->subsetFaceVertexOffsets[face] + vert;
	d->tangents[subsetFvIdx] =
	    glm::vec4(tangent[0], tangent[1], tangent[2], sign);
}

// Returns a flat array of tangents indexed by subset-local face-vertex
// position. Returns an empty vector on failure.
static std::vector<glm::vec4> generateMikkTSpaceTangents(
    const UsdMeshData& data, const VtArray<int>& faceIndices) {
	MikkTSpaceContext mctx;
	mctx.data = &data;
	mctx.faceIndices = &faceIndices;

	// Global faceStart offsets (whole mesh)
	mctx.faceStartOffsets.resize(data.faceVertexCounts.size());
	size_t off = 0;
	for (size_t i = 0; i < data.faceVertexCounts.size(); ++i) {
		mctx.faceStartOffsets[i] = off;
		off += data.faceVertexCounts[i];
	}

	// Cumulative face-vertex offsets within this subset
	mctx.subsetFaceVertexOffsets.resize(faceIndices.size() + 1);
	mctx.subsetFaceVertexOffsets[0] = 0;
	for (size_t i = 0; i < faceIndices.size(); ++i) {
		int gfi = faceIndices[i];
		mctx.subsetFaceVertexOffsets[i + 1] =
		    mctx.subsetFaceVertexOffsets[i] + data.faceVertexCounts[gfi];
	}
	size_t totalFV = mctx.subsetFaceVertexOffsets.back();
	mctx.tangents.assign(totalFV, glm::vec4(0.0f));

	SMikkTSpaceInterface iface{};
	iface.m_getNumFaces = mikkGetNumFaces;
	iface.m_getNumVerticesOfFace = mikkGetNumVerticesOfFace;
	iface.m_getPosition = mikkGetPosition;
	iface.m_getNormal = mikkGetNormal;
	iface.m_getTexCoord = mikkGetTexCoord;
	iface.m_setTSpaceBasic = mikkSetTSpaceBasic;
	iface.m_setTSpace = nullptr;

	SMikkTSpaceContext mikkCtx{};
	mikkCtx.m_pInterface = &iface;
	mikkCtx.m_pUserData = &mctx;

	if (!genTangSpaceDefault(&mikkCtx)) {
		std::cerr << "MikkTSpace tangent generation failed" << std::endl;
		return {};
	}
	return std::move(mctx.tangents);
}

UsdMeshData loadUsdMeshData(const UsdGeomMesh& mesh) {
	UsdMeshData data;

	mesh.GetPointsAttr().Get(&data.points);

	UsdGeomPrimvarsAPI primvarsAPI(mesh.GetPrim());

	{
		UsdGeomPrimvar normalPrimvar =
		    primvarsAPI.GetPrimvar(UsdGeomTokens->normals);
		data.hasNormals = normalPrimvar && normalPrimvar.HasValue() &&
		                  normalPrimvar.ComputeFlattened(&data.normals);
		data.normalInterpolation = data.hasNormals
		                               ? normalPrimvar.GetInterpolation()
		                               : UsdGeomTokens->vertex;
	}
	const TfToken uvNames[] = {TfToken("st"), TfToken("uv"), TfToken("UVMap")};
	for (const TfToken& uvName : uvNames) {
		UsdGeomPrimvar uvPrimvar = primvarsAPI.GetPrimvar(uvName);
		if (uvPrimvar && uvPrimvar.HasValue() &&
		    uvPrimvar.ComputeFlattened(&data.uvs)) {
			data.hasUVs = true;
			data.uvInterpolation = uvPrimvar.GetInterpolation();
			std::cout << "  Found UVs as '" << uvName << "' with "
			          << data.uvInterpolation << " interpolation" << std::endl;
			break;
		}
	}

	UsdGeomPrimvar tangentPrimvar = primvarsAPI.GetPrimvar(TfToken("tangents"));
	UsdGeomPrimvar signPrimvar =
	    primvarsAPI.GetPrimvar(TfToken("tangentSigns"));

	if (tangentPrimvar && tangentPrimvar.HasValue()) {
		tangentPrimvar.ComputeFlattened(&data.tangents);
		if (signPrimvar && signPrimvar.HasValue()) {
			signPrimvar.ComputeFlattened(&data.tangentSigns);
		}
		data.hasTangents = !data.tangents.empty();
		if (data.hasTangents) {
			data.tangentInterpolation = tangentPrimvar.GetInterpolation();
			std::cout << "  Found USD tangents (" << data.tangents.size() << ")"
			          << std::endl;
		}
	}

	mesh.GetFaceVertexCountsAttr().Get(&data.faceVertexCounts);
	mesh.GetFaceVertexIndicesAttr().Get(&data.faceVertexIndices);

	return data;
}

Vertex buildVertex(const UsdMeshData& data, int faceVertexIdx, int vertexIndex,
                   int faceIdx) {
	Vertex v;
	v.color = glm::vec4(1.0f);

	v.position =
	    glm::vec3(data.points[vertexIndex][0], data.points[vertexIndex][1],
	              data.points[vertexIndex][2]);

	if (data.hasNormals) {
		int normalIdx;
		if (data.normalInterpolation == UsdGeomTokens->faceVarying)
			normalIdx = faceVertexIdx;
		else if (data.normalInterpolation == UsdGeomTokens->uniform)
			normalIdx = faceIdx;
		else
			normalIdx = vertexIndex;
		if (normalIdx < (int)data.normals.size()) {
			v.normal = glm::vec3(data.normals[normalIdx][0],
			                     data.normals[normalIdx][1],
			                     data.normals[normalIdx][2]);
		} else {
			v.normal = glm::vec3(0.0f, 1.0f, 0.0f);
		}
	} else {
		v.normal = glm::vec3(0.0f, 1.0f, 0.0f);
	}

	if (data.hasUVs) {
		const int uvIdx = (data.uvInterpolation == UsdGeomTokens->faceVarying)
		                      ? faceVertexIdx
		                      : vertexIndex;
		if (uvIdx < (int)data.uvs.size()) {
			// Some USD textures have uv flip - handle shader side
			v.uv = glm::vec2(data.uvs[uvIdx][0], data.uvs[uvIdx][1]);
		} else {
			v.uv = glm::vec2(0.0f, 0.0f);
		}
	} else {
		v.uv = glm::vec2(0.0f, 0.0f);
	}

	v.tangent = glm::vec4(0.0f);
	if (data.hasTangents) {
		const int tangentIdx =
		    (data.tangentInterpolation == UsdGeomTokens->faceVarying)
		        ? faceVertexIdx
		        : vertexIndex;
		if (tangentIdx < (int)data.tangents.size()) {
			float sign = (tangentIdx < (int)data.tangentSigns.size())
			                 ? data.tangentSigns[tangentIdx]
			                 : 1.0f;
			v.tangent = glm::vec4(data.tangents[tangentIdx][0],
			                      data.tangents[tangentIdx][1],
			                      data.tangents[tangentIdx][2], sign);
		}
	}

	return v;
}

void triangulateFaces(const UsdMeshData& data, const VtArray<int>& faceIndices,
                      const std::vector<glm::vec4>& precomputedTangents,
                      std::vector<Vertex>& outVertices,
                      std::vector<uint32_t>& outIndices) {
	// Pre-compute global face start offsets
	std::vector<size_t> faceStartOffsets(data.faceVertexCounts.size());
	size_t offset = 0;
	for (size_t i = 0; i < data.faceVertexCounts.size(); ++i) {
		faceStartOffsets[i] = offset;
		offset += data.faceVertexCounts[i];
	}

	// Cumulative face-vertex offsets within this subset (mirrors
	// MikkTSpaceContext)
	std::vector<size_t> subsetFvOffsets(faceIndices.size() + 1);
	subsetFvOffsets[0] = 0;
	for (size_t i = 0; i < faceIndices.size(); ++i) {
		int gfi = faceIndices[i];
		subsetFvOffsets[i + 1] =
		    subsetFvOffsets[i] +
		    (gfi >= 0 && gfi < (int)data.faceVertexCounts.size()
		         ? data.faceVertexCounts[gfi]
		         : 0);
	}

	// Pre-allocate exact output size to avoid push_back reallocations
	size_t triVertCount = 0;
	for (int faceIdx : faceIndices) {
		if (faceIdx >= 0 && faceIdx < (int)data.faceVertexCounts.size()) {
			const int n = data.faceVertexCounts[faceIdx];
			if (n >= 3) triVertCount += (size_t)(n - 2) * 3;
		}
	}
	outVertices.reserve(outVertices.size() + triVertCount);
	outIndices.reserve(outIndices.size() + triVertCount);

	for (size_t si = 0; si < faceIndices.size(); ++si) {
		const int faceIdx = faceIndices[si];
		if (faceIdx >= (int)data.faceVertexCounts.size()) continue;

		const size_t faceStart = faceStartOffsets[faceIdx];
		const int faceVertexCount = data.faceVertexCounts[faceIdx];
		if (faceVertexCount < 3) continue;

		// Fan triangulation
		for (int tri = 0; tri < faceVertexCount - 2; ++tri) {
			const int localIndices[3] = {0, tri + 1, tri + 2};
			for (int i = 0; i < 3; ++i) {
				const int origFaceVert = localIndices[i];
				const int faceVertexIdx = (int)(faceStart + origFaceVert);
				const int vertexIndex = data.faceVertexIndices[faceVertexIdx];
				Vertex v =
				    buildVertex(data, faceVertexIdx, vertexIndex, faceIdx);

				// Apply MikkTSpace-computed tangent if available
				if (!precomputedTangents.empty()) {
					const size_t subsetFvIdx =
					    subsetFvOffsets[si] + origFaceVert;
					const glm::vec4& t = precomputedTangents[subsetFvIdx];
					if (glm::dot(glm::vec3(t), glm::vec3(t)) > 1e-6f) {
						v.tangent = t;
					} else {
						// MikkTSpace produced a zero-xyz tangent (degenerate UV
						// area, or QUAD_ONE_DEGEN_TRI missing-vert fallback
						// failed). Build any vector perpendicular to the vertex
						// normal so the shader doesn't try to normalize a zero
						// vector.
						const glm::vec3 n = glm::normalize(v.normal);
						const glm::vec3 ref = (std::abs(n.x) < 0.9f)
						                          ? glm::vec3(1.0f, 0.0f, 0.0f)
						                          : glm::vec3(0.0f, 1.0f, 0.0f);
						v.tangent = glm::vec4(
						    glm::normalize(ref - glm::dot(ref, n) * n), 1.0f);
					}
				}

				outIndices.push_back((uint32_t)outVertices.size());
				outVertices.push_back(v);
			}
		}
	}
}

uint32_t processSubset(const UsdMeshData& data, const UsdGeomSubset& subset,
                       RenderSystem& renderSystem, const glm::mat4& worldMat) {
	VtArray<int> subsetFaceIndices;
	subset.GetIndicesAttr().Get(&subsetFaceIndices);

	// Generate tangents on original USD faces BEFORE triangulation
	std::vector<glm::vec4> tangents;
	if (!data.hasTangents) {
		if (data.hasUVs) {
			tangents = generateMikkTSpaceTangents(data, subsetFaceIndices);
		} else {
			std::cerr << "  Warning: no UVs for tangent generation on subset"
			          << std::endl;
		}
	}

	std::vector<Vertex> vertices;
	std::vector<uint32_t> indices;
	triangulateFaces(data, subsetFaceIndices, tangents, vertices, indices);
	applyWorldTransform(vertices, worldMat);
	return renderSystem.uploadMesh(vertices, indices);
}

}  // namespace

Entity SceneLoader::traverseUsdPrim(
    const UsdPrim& prim, World& world, RenderSystem& renderSystem,
    const std::unordered_map<SdfPath, uint32_t, SdfPath::Hash>& materialMap,
    Entity parent, UsdGeomXformCache& xformCache) {
	if (!prim.IsA<UsdGeomImageable>()) {
		return NULL_ENTITY;
	}

	Entity entity = world.createEntity();

	if (isUsdGeometry(prim)) {
		// Geometry entities get identity transforms; all spatial data is baked
		// into world space during mesh creation. No setParent — the entity has
		// no meaningful local transform to propagate at runtime.
		world.addComponent(entity, Transform{});
	} else {
		// Non-geometry prims (Xform groups, etc.) keep their local transforms
		// and hierarchy for potential runtime use.
		Transform transform;
		extractUsdTransform(prim, transform);
		world.addComponent(entity, transform);
		if (parent != NULL_ENTITY) {
			world.setParent(entity, parent);
		}
	}

	// Check if this prim has geometry
	if (isUsdGeometry(prim)) {
		// Get the exact USD world transform via XformCache — bypasses TRS
		// decomposition round-trip issues in extractUsdTransform.
		const glm::mat4 worldMat =
		    gfMatrixToGlm(xformCache.GetLocalToWorldTransform(prim));
		// Check for GeomSubsets (multi-material meshes)
		if (prim.IsA<UsdGeomMesh>()) {
			UsdGeomMesh mesh(prim);
			std::vector<UsdGeomSubset> subsets =
			    UsdGeomSubset::GetGeomSubsets(mesh);

			if (!subsets.empty()) {
				// Mesh has subsets - create child entities for each subset
				std::cout << "  Mesh " << prim.GetPath() << " has "
				          << subsets.size()
				          << " subsets, creating child entities" << std::endl;

				// Load mesh data once, shared across all subsets
				UsdMeshData meshData = loadUsdMeshData(mesh);

				for (const UsdGeomSubset& subset : subsets) {
					// Create child entity for this subset
					Entity subsetEntity = world.createEntity();

					// Child inherits parent's transform (identity local
					// transform). Parent entity also has identity, so no
					// double-application of the world transform occurs.
					Transform childTransform;
					world.addComponent(subsetEntity, childTransform);
					world.setParent(subsetEntity, entity);

					uint32_t meshID =
					    processSubset(meshData, subset, renderSystem, worldMat);

					MeshRenderer renderer;
					renderer.meshID = meshID;
					world.addComponent(subsetEntity, renderer);

					// Bind material from subset
					MaterialBinding binding;
					binding.materialID = 0;  // Default

					UsdShadeMaterialBindingAPI subsetBindingAPI(
					    subset.GetPrim());
					UsdShadeMaterial boundMaterial =
					    subsetBindingAPI.ComputeBoundMaterial();

					if (boundMaterial) {
						SdfPath materialPath = boundMaterial.GetPath();
						auto it = materialMap.find(materialPath);
						if (it != materialMap.end()) {
							binding.materialID = it->second;
							std::cout << "    Subset " << subset.GetPath()
							          << " -> Material ID "
							          << binding.materialID << std::endl;
						} else {
							std::cout << "    Subset " << subset.GetPath()
							          << " -> Material " << materialPath
							          << " NOT FOUND" << std::endl;
						}
					}

					world.addComponent(subsetEntity, binding);
				}
			} else {
				// No subsets, handle as single mesh
				uint32_t meshID =
				    createMeshFromUsdGeom(prim, renderSystem, worldMat);

				MeshRenderer renderer;
				renderer.meshID = meshID;
				world.addComponent(entity, renderer);

				// Try to bind material
				MaterialBinding binding;
				binding.materialID = 0;

				UsdShadeMaterialBindingAPI bindingAPI(prim);
				UsdShadeMaterial boundMaterial =
				    bindingAPI.ComputeBoundMaterial();

				if (boundMaterial) {
					SdfPath materialPath = boundMaterial.GetPath();
					auto it = materialMap.find(materialPath);
					if (it != materialMap.end()) {
						binding.materialID = it->second;
					}
				}

				world.addComponent(entity, binding);
			}
		} else {
			// Non-mesh geometry (Cube, Sphere, etc.)
			uint32_t meshID =
			    createMeshFromUsdGeom(prim, renderSystem, worldMat);

			MeshRenderer renderer;
			renderer.meshID = meshID;
			world.addComponent(entity, renderer);

			MaterialBinding binding;
			binding.materialID = 0;
			world.addComponent(entity, binding);
		}
	}

	// Recurse to children
	for (const UsdPrim& child : prim.GetChildren()) {
		traverseUsdPrim(child, world, renderSystem, materialMap, entity,
		                xformCache);
	}

	return entity;
}

void SceneLoader::extractUsdTransform(const UsdPrim& prim,
                                      Transform& transform) {
	if (!prim.IsValid()) {
		std::cerr << "Invalid prim passed to extractUsdTransform" << std::endl;
		return;
	}

	// Check if this prim has transform ops
	UsdGeomXformable xformable(prim);
	if (!xformable) {
		return;  // Not a transformable prim
	}

	// Get local transformation matrix
	GfMatrix4d localMatrix;
	bool resetsXformStack;
	if (!xformable.GetLocalTransformation(&localMatrix, &resetsXformStack)) {
		std::cerr << "Failed to get local transformation for: "
		          << prim.GetPath().GetString() << std::endl;
		return;
	}

	GfMatrix4d r, u, p;
	GfVec3d s, t;
	localMatrix.Factor(&r, &s, &u, &t, &p);

	transform.position = glm::vec3(t[0], t[1], t[2]);
	transform.scale = glm::vec3(s[0], s[1], s[2]);

	GfMatrix4d rotationMatrix = localMatrix.RemoveScaleShear();
	rotationMatrix.SetTranslateOnly(GfVec3d(0, 0, 0));
	GfQuatd quat = rotationMatrix.ExtractRotationQuat();

	transform.rotation = glm::quat(static_cast<float>(quat.GetReal()),
	                               static_cast<float>(quat.GetImaginary()[0]),
	                               static_cast<float>(quat.GetImaginary()[1]),
	                               static_cast<float>(quat.GetImaginary()[2]));
}

bool SceneLoader::isUsdGeometry(const UsdPrim& prim) {
	return prim.IsA<UsdGeomMesh>() || prim.IsA<UsdGeomCube>() ||
	       prim.IsA<UsdGeomSphere>() || prim.IsA<UsdGeomCone>() ||
	       prim.IsA<UsdGeomCylinder>();
}

uint32_t SceneLoader::createMeshFromUsdGeom(const UsdPrim& prim,
                                            RenderSystem& renderSystem,
                                            const glm::mat4& worldMat) {
	if (prim.IsA<UsdGeomCube>()) return 0;
	if (prim.IsA<UsdGeomSphere>()) return 1;
	if (prim.IsA<UsdGeomCone>()) return 2;
	if (prim.IsA<UsdGeomCylinder>()) return 3;

	if (!prim.IsA<UsdGeomMesh>()) {
		std::cerr << "Prim is not a mesh: " << prim.GetPath() << std::endl;
		return 0;
	}

	const UsdMeshData data = loadUsdMeshData(UsdGeomMesh(prim));

	VtArray<int> faceIndices;
	faceIndices.resize(data.faceVertexCounts.size());
	for (size_t i = 0; i < faceIndices.size(); ++i) faceIndices[i] = (int)i;

	// Generate tangents on original USD faces BEFORE triangulation
	std::vector<glm::vec4> tangents;
	if (!data.hasTangents) {
		if (data.hasUVs) {
			tangents = generateMikkTSpaceTangents(data, faceIndices);
		} else {
			std::cerr << "  Warning: no UVs for tangent generation on "
			          << prim.GetPath() << std::endl;
		}
	}

	std::vector<Vertex> vertices;
	std::vector<uint32_t> indices;
	triangulateFaces(data, faceIndices, tangents, vertices, indices);
	applyWorldTransform(vertices, worldMat);

	std::cout << "  Loaded mesh: " << prim.GetPath() << " (" << vertices.size()
	          << " vertices, " << indices.size() / 3 << " triangles, "
	          << (data.hasUVs ? "has UVs" : "NO UVs") << ")" << std::endl;

	return renderSystem.uploadMesh(vertices, indices);
}

uint32_t SceneLoader::createMeshFromUsdGeomSubset(const UsdPrim& meshPrim,
                                                  const UsdGeomSubset& subset,
                                                  RenderSystem& renderSystem,
                                                  const glm::mat4& worldMat) {
	const UsdMeshData data = loadUsdMeshData(UsdGeomMesh(meshPrim));

	VtArray<int> subsetFaceIndices;
	subset.GetIndicesAttr().Get(&subsetFaceIndices);

	// Generate tangents on original USD faces BEFORE triangulation
	std::vector<glm::vec4> tangents;
	if (!data.hasTangents && data.hasUVs) {
		tangents = generateMikkTSpaceTangents(data, subsetFaceIndices);
	}

	std::vector<Vertex> vertices;
	std::vector<uint32_t> indices;
	triangulateFaces(data, subsetFaceIndices, tangents, vertices, indices);
	applyWorldTransform(vertices, worldMat);

	return renderSystem.uploadMesh(vertices, indices);
}