#include "boundingbox.h"
#include "material.h"
#include "rendersystem.h"
#include "sceneloader.h"
#include "transform.h"

// Decomposes a matrix into the TRS that Transform can represent. Shear and
// mirroring are lost - the same limitation as extractUsdTransform's
// GfMatrix4d::Factor path below.
static void decomposeToTRS(const glm::mat4& m, Transform& out) {
	out.position = glm::vec3(m[3]);

	const glm::vec3 scale(glm::length(glm::vec3(m[0])),
	                      glm::length(glm::vec3(m[1])),
	                      glm::length(glm::vec3(m[2])));
	out.scale = scale;

	if (scale.x <= 0.0f || scale.y <= 0.0f || scale.z <= 0.0f) {
		out.rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
		return;
	}

	out.rotation = glm::quat_cast(glm::mat3(glm::vec3(m[0]) / scale.x,
	                                        glm::vec3(m[1]) / scale.y,
	                                        glm::vec3(m[2]) / scale.z));
}

Entity SceneLoader::traverseUsdPrim(
    const UsdPrim& prim, World& world, Entity parent, float sceneScale,
    RenderSystem& renderSystem,
    const std::unordered_map<SdfPath, uint32_t, SdfPath::Hash>& materialMap,
    UsdGeomXformCache& xformCache, std::vector<MeshWorkItem>& outWorkItems) {
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
		    glm::scale(glm::mat4(1.0f), glm::vec3(sceneScale)) *
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
					item.meshData = meshData;  // VtArray CoW - cheap copy
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
			// Primitive geometry - inline fast-path, no work item needed.
			//
			// Unlike UsdGeomMesh, these resolve to built-in LOCAL-space meshes
			// shared by every prim that uses them, so worldMat cannot be baked
			// into the vertices. This branch carries the placement on the
			// entity's Transform instead. The entity is unparented, so after
			// TransformSystem::update its worldMatrix == worldMat, and
			// BoundsSystem derives the world box from the same matrix the
			// shader uses.
			const uint32_t meshID = builtinMeshForPrim(prim);
			const AABB localAABB = renderSystem.getMeshAABB(meshID);

			if (meshID == RenderSystem::INVALID_MESH_ID ||
			    !localAABB.isValid()) {
				// No MeshRenderer and no BoundingBox: an invisible entity is
				// better than one whose garbage bounds poison m_sceneBounds
				// and, through it, the cascade near plane.
				std::cerr << "  Unsupported primitive prim, skipped: "
				          << prim.GetPath() << std::endl;
			} else {
				decomposeToTRS(worldMat, world.getComponent<Transform>(entity));

				MeshRenderer renderer;
				renderer.meshID = meshID;
				world.addComponent(entity, renderer);
				world.addComponent(entity, MaterialBinding{});

				BoundingBox bb;
				bb.local = localAABB;
				bb.world = localAABB.transformed(worldMat);
				world.addComponent(entity, bb);
			}
		}
	}

	for (const UsdPrim& child : prim.GetChildren()) {
		traverseUsdPrim(child, world, entity, sceneScale, renderSystem,
		                materialMap, xformCache, outWorkItems);
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

uint32_t SceneLoader::builtinMeshForPrim(const UsdPrim& prim) {
	if (prim.IsA<UsdGeomCube>()) return static_cast<uint32_t>(BuiltinMesh::Cube);
	if (prim.IsA<UsdGeomSphere>())
		return static_cast<uint32_t>(BuiltinMesh::Sphere);
	if (prim.IsA<UsdGeomCone>()) return static_cast<uint32_t>(BuiltinMesh::Cone);
	if (prim.IsA<UsdGeomCylinder>())
		return static_cast<uint32_t>(BuiltinMesh::Cylinder);

	return RenderSystem::INVALID_MESH_ID;
}
