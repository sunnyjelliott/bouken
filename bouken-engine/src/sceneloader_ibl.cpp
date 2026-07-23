#include "sceneloader.h"

#include <pxr/usd/sdf/assetPath.h>
#include <pxr/usd/usdLux/domeLight.h>

bool SceneLoader::composeIBLSublayer(const UsdStageRefPtr& stage,
                                     const std::string& sceneDir,
                                     const std::string& sceneStem) {
	std::filesystem::path sidecarPath =
	    std::filesystem::path(sceneDir) / (sceneStem + "_ibl.usda");

	if (!std::filesystem::exists(sidecarPath)) {
		// No sidecar for this scene - not an error, just no IBL available
		return false;
	}

	SdfLayerRefPtr rootLayer = stage->GetRootLayer();
	std::string sidecarAbsolute =
	    std::filesystem::absolute(sidecarPath).generic_string();

	rootLayer->InsertSubLayerPath(sidecarAbsolute);

	std::cout << "  Composed IBL sidecar: " << sidecarPath.filename()
	          << std::endl;
	return true;
}

std::optional<std::string> SceneLoader::loadIBL(const UsdStageRefPtr& stage) {
	for (const UsdPrim& prim : stage->Traverse()) {
		if (!prim.IsA<UsdLuxDomeLight>()) {
			continue;
		}

		UsdLuxDomeLight domeLight(prim);
		UsdAttribute texFileAttr = domeLight.GetTextureFileAttr();

		SdfAssetPath assetPath;
		if (!texFileAttr || !texFileAttr.Get(&assetPath)) {
			std::cerr << "  Dome light found but has no texture:file attribute"
			          << std::endl;
			return std::nullopt;
		}

		std::string resolvedPath = assetPath.GetResolvedPath();
		if (resolvedPath.empty()) {
			// Fall back to the unresolved asset path if resolution failed -
			// still usable if it happens to be a valid relative/absolute path
			resolvedPath = assetPath.GetAssetPath();
		}

		if (resolvedPath.empty()) {
			std::cerr << "  Dome light texture path could not be resolved"
			          << std::endl;
			return std::nullopt;
		}

		std::cout << "  Found dome light HDR: " << resolvedPath << std::endl;
		return resolvedPath;
	}

	// No dome light in the composed stage - soft failure, engine continues
	// without IBL
	return std::nullopt;
}