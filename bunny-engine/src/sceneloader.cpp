#include "sceneloader.h"

#include <chrono>

bool SceneLoader::loadScene(const std::string& filepath, World& world,
                            RenderSystem& renderSystem,
                            const SceneLoadOptions& options) {
	SceneFormat format = options.format;
	if (format == SceneFormat::AUTO) {
		format = detectFormat(filepath);
	}

	switch (format) {
		case SceneFormat::USD:
			return loadUSD(filepath, world, renderSystem, options);
		case SceneFormat::OBJ:
			return loadOBJ(filepath, world, renderSystem, options);
		default:
			std::cerr << "Unsupported scene file format" << filepath
			          << std::endl;
			return false;
	}
}

SceneFormat SceneLoader::detectFormat(const std::string& filepath) {
	size_t extPos = filepath.find_last_of('.');

	// no extension, can't resolve
	if (extPos == std::string::npos) {
		return SceneFormat::AUTO;
	}

	std::string ext = filepath.substr(extPos + 1);
	std::transform(ext.begin(), ext.end(), ext.begin(),
	               [](unsigned char c) { return std::tolower(c); });

	if (ext == "usd" || ext == "usda" || ext == "usdc") {
		return SceneFormat::USD;
	} else if (ext == "obj") {
		return SceneFormat::OBJ;
	}

	// no match, can't resolve
	return SceneFormat::AUTO;
}

bool SceneLoader::loadUSD(const std::string& filepath, World& world,
                          RenderSystem& renderSystem,
                          const SceneLoadOptions& options) {
	std::cout << "Loading USD scene: " << filepath << std::endl;

	std::filesystem::path absolutepath = std::filesystem::absolute(filepath);

	// Open USD stage
	auto stageStart = std::chrono::high_resolution_clock::now();
	UsdStageRefPtr stage = UsdStage::Open(absolutepath.generic_string());
	if (!stage) {
		std::cerr << "Failed to open USD stage: " << filepath << std::endl;
		return false;
	}
	auto stageEnd = std::chrono::high_resolution_clock::now();
	auto stageDuration = std::chrono::duration_cast<std::chrono::milliseconds>(
	    stageEnd - stageStart);
	std::cout << "Stage open took: " << stageDuration.count() << "ms"
	          << std::endl;

	// Get root prim
	UsdPrim rootPrim = stage->GetPseudoRoot();
	if (!rootPrim) {
		std::cerr << "USD stage has no root prim" << std::endl;
		return false;
	}

	// Check if root has children
	auto children = rootPrim.GetChildren();
	if (children.empty()) {
		std::cout << "Root prim has no children" << std::endl;
		return true;  // Not an error, just empty
	}

	// Traverse the scene hierarchy
	int entityCount = 0;
	for (const UsdPrim& child : children) {
		Entity entity =
		    traverseUsdPrim(child, world, renderSystem, options.parentEntity);
		if (entity != NULL_ENTITY) {
			entityCount++;
		}
	}

	std::cout << "Created " << entityCount << " entities from USD scene"
	          << std::endl;
	return true;
}

bool SceneLoader::loadOBJ(const std::string& filepath, World& world,
                          RenderSystem& renderSystem,
                          const SceneLoadOptions& options) {
	std::cout << "Loading OBJ: " << filepath << std::endl;

	std::cerr << "OBJ loading not yet implemented in SceneLoader" << std::endl;
	return false;
}

Entity SceneLoader::traverseUsdPrim(const UsdPrim& prim, World& world,
                                    RenderSystem& renderSystem, Entity parent) {
	if (!prim.IsActive()) {
		return NULL_ENTITY;
	}

	if (!prim.IsA<UsdGeomImageable>()) {
		for (const UsdPrim& child : prim.GetChildren()) {
			traverseUsdPrim(child, world, renderSystem, parent);
		}

		return NULL_ENTITY;
	}

	Entity entity = world.createEntity();

	Transform transform;
	extractUsdTransform(prim, transform);
	world.addComponent(entity, transform);

	if (parent != NULL_ENTITY) {
		world.setParent(entity, parent);
	}

	if (isUsdGeometry(prim)) {
		uint32_t meshID = createMeshFromUsdGeom(prim, renderSystem);

		MeshRenderer renderer;
		renderer.meshID = meshID;
		renderer.visible = true;

		world.addComponent(entity, renderer);
	}

	for (const UsdPrim& child : prim.GetChildren()) {
		traverseUsdPrim(child, world, renderSystem, entity);
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
                                            RenderSystem& renderSystem) {
	// Handle explicit mesh data
	if (prim.IsA<UsdGeomMesh>()) {
		UsdGeomMesh mesh(prim);

		// Get vertex positions
		VtArray<GfVec3f> points;
		mesh.GetPointsAttr().Get(&points);

		// Get normals (if they exist)
		VtArray<GfVec3f> normals;
		UsdAttribute normalsAttr = mesh.GetNormalsAttr();
		if (normalsAttr.HasValue()) {
			normalsAttr.Get(&normals);
		}

		// Get face vertex indices
		VtArray<int> faceVertexIndices;
		mesh.GetFaceVertexIndicesAttr().Get(&faceVertexIndices);

		// Get face vertex counts
		VtArray<int> faceVertexCounts;
		mesh.GetFaceVertexCountsAttr().Get(&faceVertexCounts);

		std::vector<Vertex> vertices;
		std::vector<uint32_t> indices;

		// Convert points to vertices
		for (size_t i = 0; i < points.size(); i++) {
			Vertex v;
			v.position = glm::vec3(points[i][0], points[i][1], points[i][2]);

			// Use USD normals if available, otherwise compute later
			if (i < normals.size()) {
				v.normal =
				    glm::vec3(normals[i][0], normals[i][1], normals[i][2]);
			} else {
				v.normal = glm::vec3(0.0f, 1.0f, 0.0f);  // Placeholder
			}

			v.color = glm::vec3(0.7f, 0.7f, 0.7f);
			vertices.push_back(v);
		}

		// Triangulate faces
		uint32_t indexOffset = 0;
		for (int faceVertexCount : faceVertexCounts) {
			if (faceVertexCount == 3) {
				// Already a triangle
				indices.push_back(faceVertexIndices[indexOffset + 0]);
				indices.push_back(faceVertexIndices[indexOffset + 1]);
				indices.push_back(faceVertexIndices[indexOffset + 2]);
			} else if (faceVertexCount == 4) {
				// Quad - split into two triangles
				indices.push_back(faceVertexIndices[indexOffset + 0]);
				indices.push_back(faceVertexIndices[indexOffset + 1]);
				indices.push_back(faceVertexIndices[indexOffset + 2]);

				indices.push_back(faceVertexIndices[indexOffset + 0]);
				indices.push_back(faceVertexIndices[indexOffset + 2]);
				indices.push_back(faceVertexIndices[indexOffset + 3]);
			} else {
				// N-gon - fan triangulation from first vertex
				for (int i = 1; i < faceVertexCount - 1; i++) {
					indices.push_back(faceVertexIndices[indexOffset + 0]);
					indices.push_back(faceVertexIndices[indexOffset + i]);
					indices.push_back(faceVertexIndices[indexOffset + i + 1]);
				}
			}
			indexOffset += faceVertexCount;
		}

		// If USD didn't have normals, compute them from triangles
		if (normals.empty()) {
			// TODO: Compute per-vertex normals from face normals
			// For now, just leave placeholder normals
		}

		// Upload to GPU
		return renderSystem.uploadMesh(vertices, indices);
	}

	// Handle schema primitives - map to hardcoded meshes
	if (prim.IsA<UsdGeomCube>()) {
		return 0;  // Cube mesh
	}

	if (prim.IsA<UsdGeomSphere>()) {
		return 1;  // Sphere mesh
	}

	if (prim.IsA<UsdGeomCone>()) {
		return 2;  // Cone mesh
	}

	if (prim.IsA<UsdGeomCylinder>()) {
		return 0;  // Fallback to cube
	}

	std::cerr << "  -> Unknown geometry type: " << prim.GetTypeName()
	          << std::endl;
	return 0;
}
