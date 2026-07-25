#include "sceneloader.h"
#include "boundingbox.h"
#include "material.h"
#include "materialmanager.h"
#include "rendersystem.h"
#include "texturemanager.h"

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

	float sceneScale = 1.0f;
	if (options.applyMetersPerUnit) {
		double metersPerUnit = 1.0;
		stage->GetMetadata(TfToken("metersPerUnit"), &metersPerUnit);
		sceneScale = static_cast<float>(metersPerUnit);
		if (sceneScale != 1.0f) {
			std::cout << "  Scene scale: " << sceneScale
			          << " meters/unit - normalizing to meters" << std::endl;
		}
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
	std::vector<MeshWorkItem> workItems;
	UsdGeomXformCache xformCache;
	for (const UsdPrim& child : rootPrim.GetChildren()) {
		traverseUsdPrim(child, world, options.parentEntity, sceneScale,
		                renderSystem, materialMap, xformCache, workItems);
	}
	std::cout << "  Traversal complete: " << workItems.size()
	          << " meshes queued for processing" << std::endl;

	auto startTime = std::chrono::high_resolution_clock::now();

	// PASS 3: Process mesh data in parallel
	std::vector<std::future<ProcessedMesh>> futures;
	futures.reserve(workItems.size());

	for (MeshWorkItem& item : workItems) {
		futures.push_back(std::async(
		    std::launch::async,
		    [](MeshWorkItem item) -> ProcessedMesh {
			    std::vector<glm::vec4> tangents;
			    if (!item.meshData.hasTangents && item.hasUVs) {
				    tangents = generateMikkTSpaceTangents(item.meshData,
				                                          item.faceIndices);
			    }

			    std::vector<Vertex> vertices;
			    std::vector<uint32_t> indices;
			    triangulateFaces(item.meshData, item.faceIndices, tangents,
			                     vertices, indices);
			    applyWorldTransform(vertices, item.worldMat);

			    AABB aabb;
			    for (const Vertex& v : vertices) {
				    aabb.min = glm::min(aabb.min, v.position);
				    aabb.max = glm::max(aabb.max, v.position);
			    }

			    return ProcessedMesh{
			        std::move(vertices), std::move(indices), aabb,
			        item.entity,         item.parentEntity,  item.materialID};
		    },
		    std::move(item)  // move into the lambda - avoids copying VtArrays
		    ));
	}

	// PASS 4: Collect results and commit to ECS + GPU buffer (serial)
	for (auto& future : futures) {
		ProcessedMesh result = future.get();

		uint32_t meshID =
		    renderSystem.uploadMesh(result.vertices, result.indices);

		MeshRenderer renderer;
		renderer.meshID = meshID;
		world.addComponent(result.entity, renderer);

		MaterialBinding binding;
		binding.materialID = result.materialID;
		world.addComponent(result.entity, binding);

		BoundingBox bb;
		bb.aabb = result.aabb;
		world.addComponent(result.entity, bb);
	}

	auto processTime = std::chrono::high_resolution_clock::now();
	std::cout << "  Mesh processing: "
	          << std::chrono::duration_cast<std::chrono::milliseconds>(
	                 processTime - startTime)
	                 .count()
	          << "ms" << std::endl;

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
