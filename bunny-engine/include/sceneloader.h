#pragma once
#include "pch.h"
#include "rendersystem.h"
#include "world.h"

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

class SceneLoader {
   public:
	static bool loadScene(const std::string& filepath, World& world,
	                      RenderSystem& renderSystem,
	                      const SceneLoadOptions& options = {});

   private:
	static SceneFormat detectFormat(const std::string& filepath);

	static bool loadUSD(const std::string& filepath, World& world,
	                    RenderSystem& renderSystem,
	                    const SceneLoadOptions& options);
	static bool loadOBJ(const std::string& filepath, World& world,
	                    RenderSystem& renderSystem,
	                    const SceneLoadOptions& options);

	static Entity traverseUsdPrim(const UsdPrim& prim, World& world,
	                              RenderSystem& renderSystem, Entity parent);
	static void extractUsdTransform(const UsdPrim& prim, Transform& transform);
	static bool isUsdGeometry(const UsdPrim& prim);
	static uint32_t createMeshFromUsdGeom(const UsdPrim& prim,
	                                      RenderSystem& renderSystem);
};