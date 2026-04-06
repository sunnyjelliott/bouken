#include "usdmeshprocessor.h"
#include "mikktspace.h"

namespace {

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

static Vertex buildVertex(const UsdMeshData& data, int faceVertexIdx,
                          int vertexIndex, int faceIdx) {
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

}  // namespace

// ---------------------------------------------------------------------------

glm::mat4 gfMatrixToGlm(const GfMatrix4d& m) {
	glm::mat4 result;
	for (int i = 0; i < 4; ++i)
		for (int j = 0; j < 4; ++j) result[i][j] = static_cast<float>(m[i][j]);
	return result;
}

void applyWorldTransform(std::vector<Vertex>& vertices,
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

std::vector<glm::vec4> generateMikkTSpaceTangents(
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
