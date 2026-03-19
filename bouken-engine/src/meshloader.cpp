#include "meshloader.h"

#include <tiny_obj_loader.h>

// de-duplication hashing for vertices
namespace std {
template <>
struct hash<Vertex> {
	size_t operator()(Vertex const& vertex) const {
		size_t h1 = hash<float>()(vertex.position.x);
		size_t h2 = hash<float>()(vertex.position.y);
		size_t h3 = hash<float>()(vertex.position.z);
		size_t h4 = hash<float>()(vertex.color.x);
		size_t h5 = hash<float>()(vertex.color.y);
		size_t h6 = hash<float>()(vertex.color.z);
		return ((h1 ^ (h2 << 1)) ^ (h3 << 2)) ^ ((h4 ^ (h5 << 1)) ^ (h6 << 2));
	}
};
}  // namespace std

bool operator==(const Vertex& lhs, const Vertex& rhs) {
	return lhs.position == rhs.position && lhs.color == rhs.color;
}

LoadedMesh MeshLoader::loadOBJ(const std::string& filepath) {
	tinyobj::attrib_t attrib;
	std::vector<tinyobj::shape_t> shapes;
	std::vector<tinyobj::material_t> materials;
	std::string warn, err;

	if (!tinyobj::LoadObj(&attrib, &shapes, &materials, &warn, &err,
	                      filepath.c_str())) {
		throw std::runtime_error("Failed to load OBJ file: " + filepath + "\n" +
		                         warn + err);
	}

	if (!warn.empty()) {
		std::cout << "OBJ Warning: " << warn << std::endl;
	}

	LoadedMesh mesh;
	std::unordered_map<Vertex, uint32_t> uniqueVertices;

	// Iterate over all shapes
	for (const auto& shape : shapes) {
		// Iterate over all faces
		for (const auto& index : shape.mesh.indices) {
			Vertex vertex{};

			// Position
			vertex.position = {attrib.vertices[3 * index.vertex_index + 0],
			                   attrib.vertices[3 * index.vertex_index + 1],
			                   attrib.vertices[3 * index.vertex_index + 2]};

			// Color (use vertex color if available, otherwise white)
			if (attrib.colors.size() > 3 * index.vertex_index + 2) {
				vertex.color = {attrib.colors[3 * index.vertex_index + 0],
				                attrib.colors[3 * index.vertex_index + 1],
				                attrib.colors[3 * index.vertex_index + 2]};
			} else {
				// Default white color
				vertex.color = {1.0f, 1.0f, 1.0f};
			}

			// Check if vertex already exists
			if (uniqueVertices.count(vertex) == 0) {
				uniqueVertices[vertex] =
				    static_cast<uint32_t>(mesh.vertices.size());
				mesh.vertices.push_back(vertex);
			}

			mesh.indices.push_back(uniqueVertices[vertex]);
		}
	}

	std::cout << "Loaded OBJ: " << filepath << std::endl;
	std::cout << "  Vertices: " << mesh.vertices.size() << std::endl;
	std::cout << "  Indices: " << mesh.indices.size() << std::endl;
	std::cout << "  Triangles: " << mesh.indices.size() / 3 << std::endl;

	return mesh;
}