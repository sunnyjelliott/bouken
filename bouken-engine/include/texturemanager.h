#pragma once
#include "pch.h"

class ITextureBackend;

struct DecodedImage {
	std::string filepath;
	unsigned char* pixels = nullptr;
	int width = 0;
	int height = 0;
	int channels = 0;
	bool success = false;
};

class TextureManager {
   public:
	void initialize(ITextureBackend* backend);
	void cleanup();

	uint32_t loadTexture(const std::string& filepath);

	std::vector<uint32_t> loadTexturesBatch(
	    const std::vector<std::string>& filepaths);

	void* getBindingData(uint32_t textureID) const;
	bool hasTexture(uint32_t textureID) const;

	// Get default texture IDs
	uint32_t getDefaultWhiteTextureID() const {
		return m_defaultWhiteTextureID;
	}
	uint32_t getDefaultNormalTextureID() const {
		return m_defaultNormalTextureID;
	}

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

	uint32_t m_defaultWhiteTextureID = 0;
	uint32_t m_defaultNormalTextureID = 0;

	static DecodedImage decodeImage(const std::string& filepath);
	uint32_t uploadDecodedImage(const DecodedImage& decoded);

	void createDefaultTextures();
};