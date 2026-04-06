#pragma once
#include "usdmeshprocessor.h"

class World;
class RenderSystem;
class TextureManager;
class MaterialManager;

enum class SceneFormat {
	AUTO,
	USD,
	OBJ,
};

struct SceneLoadOptions {
	SceneFormat format = SceneFormat::AUTO;
	bool createHeirarchy = true;
	Entity parentEntity = NULL_ENTITY;
};

struct MaterialTextureInfo {
	glm::vec3 baseColor = glm::vec3(1.0f);
	std::string albedoPath;

	glm::vec3 normal;
	std::string normalPath;

	float metallic = 0.0f;
	std::string metallicPath;

	float roughness = 0.5f;
	std::string roughnessPath;

	float occlusion = 1.0f;
	std::string aoPath;

	glm::vec3 emissiveColor = glm::vec3(0.0f);
	std::string emissivePath;

	float opacityThreshold = 0.0f;
	std::string opacityPath;
};

class SceneLoader {
   public:
	static bool loadScene(const std::string& filepath, World& world,
	                      RenderSystem& renderSystem,
	                      TextureManager& textureManager,
	                      MaterialManager& materialManager,
	                      const SceneLoadOptions& options = {});

   private:
	// File Parsing
	static SceneFormat detectFormat(const std::string& filepath);
	static bool loadUSD(const std::string& filepath, World& world,
	                    RenderSystem& renderSystem,
	                    TextureManager& textureManager,
	                    MaterialManager& materialManager,
	                    const SceneLoadOptions& options);
	static bool loadOBJ(const std::string& filepath, World& world,
	                    RenderSystem& renderSystem,
	                    TextureManager& textureManager,
	                    MaterialManager& materialManager,
	                    const SceneLoadOptions& options);

	// USD Materials/Textures
	static void parseUsdMaterials(
	    const UsdStageRefPtr& stage, MaterialManager& materialManager,
	    TextureManager& textureManager, const std::string& sceneDir,
	    std::unordered_map<SdfPath, uint32_t, SdfPath::Hash>& materialMap);
	static std::string resolveInputTexturePath(const UsdShadeInput& input,
	                                           const std::string& sceneDir);
	static std::string extractTextureFromShader(const UsdShadeShader& shader,
	                                            const std::string& sceneDir);
	static MaterialTextureInfo extractMaterialTextureInfo(
	    const UsdShadeShader& shader, const std::string& sceneDir);

	// USD Geometry
	static Entity traverseUsdPrim(
	    const UsdPrim& prim, World& world, RenderSystem& renderSystem,
	    const std::unordered_map<SdfPath, uint32_t, SdfPath::Hash>& materialMap,
	    Entity parent, UsdGeomXformCache& xformCache,
	    std::vector<MeshWorkItem>& outWorkItems);
	static void extractUsdTransform(const UsdPrim& prim, Transform& transform);
	static bool isUsdGeometry(const UsdPrim& prim);
	static uint32_t createMeshFromUsdGeom(const UsdPrim& prim,
	                                      RenderSystem& renderSystem,
	                                      const glm::mat4& worldMat);
	static uint32_t createMeshFromUsdGeomSubset(const UsdPrim& meshPrim,
	                                            const UsdGeomSubset& subset,
	                                            RenderSystem& renderSystem,
	                                            const glm::mat4& worldMat);
};