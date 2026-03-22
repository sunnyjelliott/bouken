#include "texturemanager.h"
#include "itexturebackend.h"
#include "vulkantexturebackend.h"

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

void TextureManager::initialize(ITextureBackend* backend) {
	m_backend = backend;
	createDefaultTextures();
	std::cout << "TextureManager initialized" << std::endl;
}

void TextureManager::cleanup() {
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

void TextureManager::createDefaultTextures() {
	// Register default textures from backend
	VulkanTextureBackend* vulkanBackend =
	    dynamic_cast<VulkanTextureBackend*>(m_backend);
	if (vulkanBackend) {
		TextureMetadata whiteMeta;
		whiteMeta.filepath = "[default_white]";
		whiteMeta.width = 1;
		whiteMeta.height = 1;
		whiteMeta.backendHandle = vulkanBackend->getDefaultWhiteTexture();

		m_defaultWhiteTextureID = m_nextID++;
		m_textures[m_defaultWhiteTextureID] = whiteMeta;
		m_pathToID["[default_white]"] = m_defaultWhiteTextureID;

		TextureMetadata normalMeta;
		normalMeta.filepath = "[default_normal]";
		normalMeta.width = 1;
		normalMeta.height = 1;
		normalMeta.backendHandle = vulkanBackend->getDefaultNormalTexture();

		m_defaultNormalTextureID = m_nextID++;
		m_textures[m_defaultNormalTextureID] = normalMeta;
		m_pathToID["[default_normal]"] = m_defaultNormalTextureID;

		std::cout << "  Default white texture ID: " << m_defaultWhiteTextureID
		          << std::endl;
		std::cout << "  Default normal texture ID: " << m_defaultNormalTextureID
		          << std::endl;
	}
}