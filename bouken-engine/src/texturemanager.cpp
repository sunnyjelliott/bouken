#include "texturemanager.h"
#include "itexturebackend.h"

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

void TextureManager::initialize(ITextureBackend* backend) {
	m_backend = backend;
	std::cout << "TextureManager initialized" << std::endl;
}

void TextureManager::cleanup() {
	// Destroy all textures via backend
	for (auto& [id, metadata] : m_textures) {
		m_backend->destroyTexture(metadata.backendHandle);
	}

	m_textures.clear();
	m_pathToID.clear();
	m_nextID = 1;

	std::cout << "TextureManager cleaned up" << std::endl;
}

uint32_t TextureManager::loadTexture(const std::string& filepath) {
	// Check if already loaded (deduplication)
	auto it = m_pathToID.find(filepath);
	if (it != m_pathToID.end()) {
		std::cout << "Texture already loaded: " << filepath
		          << " (ID: " << it->second << ")" << std::endl;
		return it->second;
	}

	// Load image data
	int width, height, channels;
	stbi_uc* pixels =
	    stbi_load(filepath.c_str(), &width, &height, &channels, STBI_rgb_alpha);

	if (!pixels) {
		std::cerr << "Failed to load texture: " << filepath << std::endl;
		return 0;  // no texture id
	}

	// Create texture via backend
	TextureCreateInfo createInfo;
	createInfo.pixels = pixels;
	createInfo.width = static_cast<uint32_t>(width);
	createInfo.height = static_cast<uint32_t>(height);
	createInfo.channels = 4;  // forced RGBA with STBI_rgb_alpha
	createInfo.generateMipmaps = false;

	BackendTextureHandle backendHandle = m_backend->createTexture(createInfo);

	stbi_image_free(pixels);

	if (!backendHandle) {
		std::cerr << "Backend failed to create texture: " << filepath
		          << std::endl;
		return 0;
	}

	// Store metadata
	uint32_t textureID = m_nextID++;
	TextureMetadata metadata;
	metadata.filepath = filepath;
	metadata.width = createInfo.width;
	metadata.height = createInfo.height;
	metadata.backendHandle = backendHandle;

	m_textures[textureID] = metadata;
	m_pathToID[filepath] = textureID;

	std::cout << "Loaded texture: " << filepath << " (" << width << "x"
	          << height << ")"
	          << " ID: " << textureID << std::endl;

	return textureID;
}

void* TextureManager::getBindingData(uint32_t textureID) const {
	auto it = m_textures.find(textureID);
	if (it == m_textures.end()) {
		return nullptr;
	}

	return m_backend->getBindingData(it->second.backendHandle);
}

bool TextureManager::hasTexture(uint32_t textureID) const {
	return m_textures.find(textureID) != m_textures.end();
}