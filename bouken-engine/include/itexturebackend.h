#pragma once
#include "pch.h"

using BackendTextureHandle = void*;

struct TextureCreateInfo {
	const void* pixels;
	uint32_t width;
	uint32_t height;
	uint32_t channels;
	bool generateMipmaps = false;
	bool sRGB = true;
};

class ITextureBackend {
   public:
	virtual ~ITextureBackend() = default;

	virtual BackendTextureHandle createTexture(
	    const TextureCreateInfo& info) = 0;

	virtual void destroyTexture(BackendTextureHandle handle) = 0;

	virtual void* getBindingData(BackendTextureHandle handle) = 0;
};