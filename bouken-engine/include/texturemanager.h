#pragma once
#include "pch.h"

class ITextureBackend;

class TextureManager {
   public:
	void initialize(ITextureBackend* backend);
	void cleanup();

	uint32_t loadTexture(const std::string& filepath);

	void* getBindingData(uint32_t textureID) const;

	bool hasTexture(uint32_t textureID) const;

   private:
	struct TextureMetadata {
		std::string filepath;
		uint32_t width;
		uint32_t height;
		void* backendHandle;
	};

	ITextureBackend* m_backend = nullptr;

	std::unordered_map<std::string, uint32_t> m_pathToID;
	std::unordered_map<uint32_t, TextureMetadata> m_textures;
	uint32_t m_nextID = 1;
};