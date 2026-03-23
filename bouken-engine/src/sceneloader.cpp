#include "sceneloader.h"
#include "material.h"
#include "materialmanager.h"
#include "primitives.h"
#include "rendersystem.h"
#include "texturemanager.h"
#include "transform.h"

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
	for (const UsdPrim& child : rootPrim.GetChildren()) {
		traverseUsdPrim(child, world, renderSystem, materialMap,
		                options.parentEntity);
	}

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

	// Traverse all prims looking for materials
	for (const UsdPrim& prim : stage->Traverse()) {
		if (!prim.IsA<UsdShadeMaterial>()) {
			continue;
		}

		UsdShadeMaterial shadeMaterial(prim);
		UsdShadeShader surfaceShader = shadeMaterial.ComputeSurfaceSource();

		if (!surfaceShader) {
			std::cout << "  Material has no surface shader: " << prim.GetPath()
			          << std::endl;
			continue;
		}

		uint32_t materialID = extractMaterialFromShader(
		    surfaceShader, materialManager, textureManager, sceneDir);

		materialMap[prim.GetPath()] = materialID;
		std::cout << "  Registered material: " << prim.GetPath() << " -> ID "
		          << materialID << std::endl;
	}
}

uint32_t SceneLoader::extractMaterialFromShader(
    const UsdShadeShader& shader, MaterialManager& materialManager,
    TextureManager& textureManager, const std::string& sceneDir) {
	// Only handle UsdPreviewSurface for real-time rendering
	TfToken shaderId;
	shader.GetIdAttr().Get(&shaderId);

	if (shaderId != TfToken("UsdPreviewSurface")) {
		std::cout << "  Unsupported shader type: " << shaderId
		          << " (only UsdPreviewSurface supported)" << std::endl;
		// Return default material
		return 0;
	}

	Material material;
	material.baseColor = glm::vec3(0.8f);

	std::cout << "  Parsing UsdPreviewSurface shader..." << std::endl;

	// Track which inputs we've handled
	std::set<TfToken> handledInputs;

	// ===== diffuseColor (albedo/baseColor) =====
	UsdShadeInput diffuseInput = shader.GetInput(TfToken("diffuseColor"));
	if (diffuseInput) {
		handledInputs.insert(TfToken("diffuseColor"));

		// Try to get direct color value
		GfVec3f colorValue;
		if (diffuseInput.Get(&colorValue)) {
			material.baseColor =
			    glm::vec3(colorValue[0], colorValue[1], colorValue[2]);
			std::cout << "    diffuseColor (direct): (" << colorValue[0] << ", "
			          << colorValue[1] << ", " << colorValue[2] << ")"
			          << std::endl;
		}

		// Check for texture connection
		std::string texturePath =
		    resolveInputTexturePath(diffuseInput, sceneDir);
		if (!texturePath.empty()) {
			material.albedoTextureID = textureManager.loadTexture(texturePath);
			std::cout << "    diffuseColor (texture): " << texturePath
			          << std::endl;
		}
	}

	// ===== normal =====
	UsdShadeInput normalInput = shader.GetInput(TfToken("normal"));
	if (normalInput) {
		handledInputs.insert(TfToken("normal"));

		std::string texturePath =
		    resolveInputTexturePath(normalInput, sceneDir);
		if (!texturePath.empty()) {
			material.normalTextureID = textureManager.loadTexture(texturePath);
			std::cout << "    normal (texture): " << texturePath << std::endl;
		}
	}

	// ===== metallic (TODO: not yet in Material struct) =====
	UsdShadeInput metallicInput = shader.GetInput(TfToken("metallic"));
	if (metallicInput) {
		handledInputs.insert(TfToken("metallic"));

		float metallicValue;
		if (metallicInput.Get(&metallicValue)) {
			std::cout << "    metallic (value): " << metallicValue
			          << " [NOT BOUND - TODO]" << std::endl;
		}

		std::string texturePath =
		    resolveInputTexturePath(metallicInput, sceneDir);
		if (!texturePath.empty()) {
			std::cout << "    metallic (texture): " << texturePath
			          << " [NOT BOUND - TODO]" << std::endl;
		}
	}

	// ===== roughness (TODO: not yet in Material struct) =====
	UsdShadeInput roughnessInput = shader.GetInput(TfToken("roughness"));
	if (roughnessInput) {
		handledInputs.insert(TfToken("roughness"));

		float roughnessValue;
		if (roughnessInput.Get(&roughnessValue)) {
			std::cout << "    roughness (value): " << roughnessValue
			          << " [NOT BOUND - TODO]" << std::endl;
		}

		std::string texturePath =
		    resolveInputTexturePath(roughnessInput, sceneDir);
		if (!texturePath.empty()) {
			std::cout << "    roughness (texture): " << texturePath
			          << " [NOT BOUND - TODO]" << std::endl;
		}
	}

	// ===== occlusion (TODO: not yet in Material struct) =====
	UsdShadeInput occlusionInput = shader.GetInput(TfToken("occlusion"));
	if (occlusionInput) {
		handledInputs.insert(TfToken("occlusion"));

		float occlusionValue;
		if (occlusionInput.Get(&occlusionValue)) {
			std::cout << "    occlusion (value): " << occlusionValue
			          << " [NOT BOUND - TODO]" << std::endl;
		}

		std::string texturePath =
		    resolveInputTexturePath(occlusionInput, sceneDir);
		if (!texturePath.empty()) {
			std::cout << "    occlusion (texture): " << texturePath
			          << " [NOT BOUND - TODO]" << std::endl;
		}
	}

	// ===== emissiveColor (TODO: not yet in Material struct) =====
	UsdShadeInput emissiveInput = shader.GetInput(TfToken("emissiveColor"));
	if (emissiveInput) {
		handledInputs.insert(TfToken("emissiveColor"));

		GfVec3f emissiveValue;
		if (emissiveInput.Get(&emissiveValue)) {
			std::cout << "    emissiveColor (value): (" << emissiveValue[0]
			          << ", " << emissiveValue[1] << ", " << emissiveValue[2]
			          << ") [NOT BOUND - TODO]" << std::endl;
		}

		std::string texturePath =
		    resolveInputTexturePath(emissiveInput, sceneDir);
		if (!texturePath.empty()) {
			std::cout << "    emissiveColor (texture): " << texturePath
			          << " [NOT BOUND - TODO]" << std::endl;
		}
	}

	// ===== Report unhandled inputs =====
	std::vector<UsdShadeInput> allInputs = shader.GetInputs();
	for (const UsdShadeInput& input : allInputs) {
		TfToken inputName = input.GetBaseName();

		if (handledInputs.find(inputName) == handledInputs.end()) {
			std::cout << "    " << inputName << ": [UNHANDLED INPUT]"
			          << std::endl;
		}
	}

	return materialManager.createMaterial(material);
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

	// Case 1: Connected to NodeGraph
	if (sourcePrim.IsA<UsdShadeNodeGraph>()) {
		UsdShadeNodeGraph nodeGraph(sourcePrim);

		// Get the output we're connected to
		UsdShadeOutput output = nodeGraph.GetOutput(sourceName);
		if (!output) {
			return "";
		}

		// Follow the connection inside the NodeGraph
		UsdShadeConnectableAPI innerSource;
		TfToken innerSourceName;
		UsdShadeAttributeType innerSourceType;

		if (output.GetConnectedSource(&innerSource, &innerSourceName,
		                              &innerSourceType)) {
			UsdShadeShader textureShader(innerSource.GetPrim());
			if (textureShader) {
				return extractTextureFromShader(textureShader, sceneDir);
			}
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

Entity SceneLoader::traverseUsdPrim(
    const UsdPrim& prim, World& world, RenderSystem& renderSystem,
    const std::unordered_map<SdfPath, uint32_t, SdfPath::Hash>& materialMap,
    Entity parent) {
	if (!prim.IsA<UsdGeomImageable>()) {
		return NULL_ENTITY;
	}

	Entity entity = world.createEntity();

	// Extract transform
	Transform transform;
	extractUsdTransform(prim, transform);
	world.addComponent(entity, transform);

	// Set up hierarchy
	if (parent != NULL_ENTITY) {
		world.setParent(entity, parent);
	}

	// Check if this prim has geometry
	if (isUsdGeometry(prim)) {
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

				for (const UsdGeomSubset& subset : subsets) {
					// Create child entity for this subset
					Entity subsetEntity = world.createEntity();

					// Child inherits parent's transform (identity local
					// transform)
					Transform childTransform;
					world.addComponent(subsetEntity, childTransform);
					world.setParent(subsetEntity, entity);

					// Extract mesh for this subset only
					uint32_t meshID =
					    createMeshFromUsdGeomSubset(prim, subset, renderSystem);

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
				// No subsets, handle as single mesh (old code path)
				uint32_t meshID = createMeshFromUsdGeom(prim, renderSystem);

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
			uint32_t meshID = createMeshFromUsdGeom(prim, renderSystem);

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
		traverseUsdPrim(child, world, renderSystem, materialMap, entity);
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

namespace {

struct UsdMeshData {
	VtArray<GfVec3f> points;
	VtArray<GfVec3f> normals;
	bool hasNormals = false;
	TfToken normalInterpolation;
	VtArray<GfVec2f> uvs;
	bool hasUVs = false;
	TfToken uvInterpolation;
	VtArray<int> faceVertexCounts;
	VtArray<int> faceVertexIndices;
};

UsdMeshData loadUsdMeshData(const UsdGeomMesh& mesh) {
	UsdMeshData data;

	mesh.GetPointsAttr().Get(&data.points);

	data.hasNormals = mesh.GetNormalsAttr().Get(&data.normals);
	data.normalInterpolation =
	    data.hasNormals ? mesh.GetNormalsInterpolation() : UsdGeomTokens->vertex;

	UsdGeomPrimvarsAPI primvarsAPI(mesh.GetPrim());
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

	mesh.GetFaceVertexCountsAttr().Get(&data.faceVertexCounts);
	mesh.GetFaceVertexIndicesAttr().Get(&data.faceVertexIndices);

	return data;
}

Vertex buildVertex(const UsdMeshData& data, int faceVertexIdx, int vertexIndex) {
	Vertex v;
	v.color = glm::vec3(1.0f);

	v.position = glm::vec3(data.points[vertexIndex][0],
	                       data.points[vertexIndex][1],
	                       data.points[vertexIndex][2]);

	if (data.hasNormals) {
		const int normalIdx =
		    (data.normalInterpolation == UsdGeomTokens->faceVarying)
		        ? faceVertexIdx
		        : vertexIndex;
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
		const int uvIdx =
		    (data.uvInterpolation == UsdGeomTokens->faceVarying) ? faceVertexIdx
		                                                          : vertexIndex;
		if (uvIdx < (int)data.uvs.size()) {
			v.uv = glm::vec2(data.uvs[uvIdx][0], data.uvs[uvIdx][1]);
		} else {
			v.uv = glm::vec2(0.0f, 0.0f);
		}
	} else {
		v.uv = glm::vec2(0.0f, 0.0f);
	}

	return v;
}

void triangulateFaces(const UsdMeshData& data, const VtArray<int>& faceIndices,
                      std::vector<Vertex>& outVertices,
                      std::vector<uint32_t>& outIndices) {
	// Pre-compute face start offsets
	std::vector<size_t> faceStartOffsets(data.faceVertexCounts.size());
	size_t offset = 0;
	for (size_t i = 0; i < data.faceVertexCounts.size(); ++i) {
		faceStartOffsets[i] = offset;
		offset += data.faceVertexCounts[i];
	}

	for (int faceIdx : faceIndices) {
		if (faceIdx >= (int)data.faceVertexCounts.size()) continue;

		const size_t faceStart = faceStartOffsets[faceIdx];
		const int faceVertexCount = data.faceVertexCounts[faceIdx];
		if (faceVertexCount < 3) continue;

		// Fan triangulation
		for (int tri = 0; tri < faceVertexCount - 2; ++tri) {
			const int localIndices[3] = {0, tri + 1, tri + 2};
			for (int i = 0; i < 3; ++i) {
				const int faceVertexIdx = (int)(faceStart + localIndices[i]);
				const int vertexIndex = data.faceVertexIndices[faceVertexIdx];
				outIndices.push_back((uint32_t)outVertices.size());
				outVertices.push_back(
				    buildVertex(data, faceVertexIdx, vertexIndex));
			}
		}
	}
}

}  // namespace

uint32_t SceneLoader::createMeshFromUsdGeom(const UsdPrim& prim,
                                            RenderSystem& renderSystem) {
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

	std::vector<Vertex> vertices;
	std::vector<uint32_t> indices;
	triangulateFaces(data, faceIndices, vertices, indices);

	std::cout << "  Loaded mesh: " << prim.GetPath() << " (" << vertices.size()
	          << " vertices, " << indices.size() / 3 << " triangles, "
	          << (data.hasUVs ? "has UVs" : "NO UVs") << ")" << std::endl;

	return renderSystem.uploadMesh(vertices, indices);
}

uint32_t SceneLoader::createMeshFromUsdGeomSubset(const UsdPrim& meshPrim,
                                                  const UsdGeomSubset& subset,
                                                  RenderSystem& renderSystem) {
	const UsdMeshData data = loadUsdMeshData(UsdGeomMesh(meshPrim));

	VtArray<int> subsetFaceIndices;
	subset.GetIndicesAttr().Get(&subsetFaceIndices);

	std::vector<Vertex> vertices;
	std::vector<uint32_t> indices;
	triangulateFaces(data, subsetFaceIndices, vertices, indices);

	return renderSystem.uploadMesh(vertices, indices);
}