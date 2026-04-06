#include "sceneloader.h"
#include "material.h"
#include "rendersystem.h"
#include "transform.h"

Entity SceneLoader::traverseUsdPrim(
    const UsdPrim& prim, World& world, RenderSystem& renderSystem,
    const std::unordered_map<SdfPath, uint32_t, SdfPath::Hash>& materialMap,
    Entity parent, UsdGeomXformCache& xformCache,
    std::vector<MeshWorkItem>& outWorkItems) {
	if (!prim.IsA<UsdGeomImageable>()) return NULL_ENTITY;

	Entity entity = world.createEntity();

	if (isUsdGeometry(prim)) {
		world.addComponent(entity, Transform{});
	} else {
		Transform transform;
		extractUsdTransform(prim, transform);
		world.addComponent(entity, transform);
		if (parent != NULL_ENTITY) world.setParent(entity, parent);
	}

	if (isUsdGeometry(prim)) {
		const glm::mat4 worldMat =
		    gfMatrixToGlm(xformCache.GetLocalToWorldTransform(prim));

		if (prim.IsA<UsdGeomMesh>()) {
			UsdGeomMesh mesh(prim);
			std::vector<UsdGeomSubset> subsets =
			    UsdGeomSubset::GetGeomSubsets(mesh);

			if (!subsets.empty()) {
				std::cout << "  Mesh " << prim.GetPath() << " has "
				          << subsets.size()
				          << " subsets, creating child entities" << std::endl;

				// loadUsdMeshData is called once here, then moved into each
				// work item. Since VtArray is copy-on-write this is cheap.
				UsdMeshData meshData = loadUsdMeshData(mesh);

				for (const UsdGeomSubset& subset : subsets) {
					Entity subsetEntity = world.createEntity();
					world.addComponent(subsetEntity, Transform{});
					world.setParent(subsetEntity, entity);

					// Resolve material now while we still have USD access
					uint32_t materialID = 0;
					UsdShadeMaterialBindingAPI bindingAPI(subset.GetPrim());
					UsdShadeMaterial boundMaterial =
					    bindingAPI.ComputeBoundMaterial();
					if (boundMaterial) {
						auto it = materialMap.find(boundMaterial.GetPath());
						if (it != materialMap.end()) {
							materialID = it->second;
							std::cout << "    Subset " << subset.GetPath()
							          << " -> Material ID " << materialID
							          << std::endl;
						} else {
							std::cout << "    Subset " << subset.GetPath()
							          << " -> Material "
							          << boundMaterial.GetPath() << " NOT FOUND"
							          << std::endl;
						}
					}

					VtArray<int> faceIndices;
					subset.GetIndicesAttr().Get(&faceIndices);

					MeshWorkItem item;
					item.meshData = meshData;  // VtArray CoW — cheap copy
					item.faceIndices = std::move(faceIndices);
					item.worldMat = worldMat;
					item.entity = subsetEntity;
					item.parentEntity = NULL_ENTITY;  // setParent already done
					item.materialID = materialID;
					item.hasUVs = meshData.hasUVs;
					outWorkItems.push_back(std::move(item));
				}
			} else {
				// UsdGeomMesh with no subsets
				uint32_t materialID = 0;
				UsdShadeMaterialBindingAPI bindingAPI(prim);
				UsdShadeMaterial boundMaterial =
				    bindingAPI.ComputeBoundMaterial();
				if (boundMaterial) {
					auto it = materialMap.find(boundMaterial.GetPath());
					if (it != materialMap.end()) materialID = it->second;
				}

				UsdMeshData meshData = loadUsdMeshData(mesh);

				// All faces
				VtArray<int> faceIndices;
				faceIndices.resize(meshData.faceVertexCounts.size());
				std::iota(faceIndices.begin(), faceIndices.end(), 0);

				MeshWorkItem item;
				item.meshData = std::move(meshData);
				item.faceIndices = std::move(faceIndices);
				item.worldMat = worldMat;
				item.entity = entity;
				item.parentEntity = NULL_ENTITY;
				item.materialID = materialID;
				item.hasUVs = item.meshData.hasUVs;
				outWorkItems.push_back(std::move(item));
			}
		} else {
			// Primitive geometry — inline fast-path, no work item needed
			uint32_t meshID =
			    createMeshFromUsdGeom(prim, renderSystem, worldMat);
			MeshRenderer renderer;
			renderer.meshID = meshID;
			world.addComponent(entity, renderer);
			world.addComponent(entity, MaterialBinding{});
		}
	}

	for (const UsdPrim& child : prim.GetChildren()) {
		traverseUsdPrim(child, world, renderSystem, materialMap, entity,
		                xformCache, outWorkItems);
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
