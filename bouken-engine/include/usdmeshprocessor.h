#pragma once
#include "pch.h"
#include "vertex.h"
#include "world.h"

// ---------------------------------------------------------------------------
// Data structures shared between USD geometry traversal and mesh processing
// ---------------------------------------------------------------------------

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

struct MeshWorkItem {
	UsdMeshData meshData;
	VtArray<int> faceIndices;
	glm::mat4 worldMat;
	Entity entity;
	Entity parentEntity;
	uint32_t materialID;
	bool hasUVs;  // cached from meshData to avoid recheck
};

struct ProcessedMesh {
	std::vector<Vertex> vertices;
	std::vector<uint32_t> indices;
	Entity entity;
	Entity parentEntity;
	uint32_t materialID;
};

// ---------------------------------------------------------------------------
// Mesh processing helpers
// ---------------------------------------------------------------------------

// Converts a GfMatrix4d to glm::mat4. USD uses row-vector convention
// (v' = v*M) and GLM uses column-vector convention (v' = M*v), but the
// memory layout for the same geometric transformation is identical - each
// convention stores "image of basis i" as the i-th block of 4 floats. A
// direct element copy (result[i][j] = m[i][j]) is correct; do NOT transpose.
glm::mat4 gfMatrixToGlm(const GfMatrix4d& m);

// Bakes a world-space transform into every vertex's position, normal, and
// tangent. Call after MikkTSpace + triangulateFaces, before uploadMesh.
void applyWorldTransform(std::vector<Vertex>& vertices,
                         const glm::mat4& worldMat);

// Returns a flat array of tangents indexed by subset-local face-vertex
// position. Returns an empty vector on failure.
std::vector<glm::vec4> generateMikkTSpaceTangents(
    const UsdMeshData& data, const VtArray<int>& faceIndices);

// Extracts all mesh attribute data from a UsdGeomMesh prim.
UsdMeshData loadUsdMeshData(const UsdGeomMesh& mesh);

// Fan-triangulates USD n-gon faces into a flat vertex/index list.
// Applies precomputedTangents (from generateMikkTSpaceTangents) when provided.
void triangulateFaces(const UsdMeshData& data, const VtArray<int>& faceIndices,
                      const std::vector<glm::vec4>& precomputedTangents,
                      std::vector<Vertex>& outVertices,
                      std::vector<uint32_t>& outIndices);
