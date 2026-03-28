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
	// Single texture - use batch with size 1
	std::vector<std::string> paths = {filepath};
	std::vector<uint32_t> results = loadTexturesBatch(paths);
	return results.empty() ? 0 : results[0];
}

std::vector<uint32_t> TextureManager::loadTexturesBatch(
    const std::vector<std::string>& filepaths,
    const std::vector<bool>& sRGBFlags) {
	auto startTime = std::chrono::high_resolution_clock::now();

	std::cout << "Batch loading " << filepaths.size() << " textures..."
	          << std::endl;

	auto getSRGB = [&](size_t i) {
		return (i < sRGBFlags.size()) ? sRGBFlags[i] : true;
	};

	// Filter out already-loaded textures
	std::vector<std::string> pathsToLoad;
	std::vector<bool> sRGBToLoad;
	std::vector<uint32_t> results(filepaths.size(), 0);

	for (size_t i = 0; i < filepaths.size(); ++i) {
		auto it = m_pathToID.find(filepaths[i]);
		if (it != m_pathToID.end()) {
			// Already loaded
			results[i] = it->second;
		} else {
			pathsToLoad.push_back(filepaths[i]);
			sRGBToLoad.push_back(getSRGB(i));
		}
	}

	if (pathsToLoad.empty()) {
		std::cout << "All textures already loaded (cached)" << std::endl;
		return results;
	}

	std::cout << "  Decoding " << pathsToLoad.size() << " new textures on "
	          << std::thread::hardware_concurrency() << " threads..."
	          << std::endl;

	// Phase 1: Decode images in parallel on CPU
	std::vector<std::future<DecodedImage>> futures;
	futures.reserve(pathsToLoad.size());

	for (const std::string& path : pathsToLoad) {
		futures.push_back(std::async(std::launch::async, decodeImage, path));
	}

	// Wait for all decodes to complete
	std::vector<DecodedImage> decodedImages;
	decodedImages.reserve(pathsToLoad.size());

	for (auto& future : futures) {
		decodedImages.push_back(future.get());
	}

	auto decodeTime = std::chrono::high_resolution_clock::now();
	auto decodeDuration = std::chrono::duration_cast<std::chrono::milliseconds>(
	    decodeTime - startTime);
	std::cout << "  Decoded in " << decodeDuration.count() << "ms" << std::endl;

	// Phase 2: Upload to GPU sequentially (Vulkan requires single-threaded)
	std::cout << "  Uploading to GPU..." << std::endl;

	size_t newTextureIdx = 0;
	for (size_t i = 0; i < filepaths.size(); ++i) {
		if (results[i] == 0) {
			// This was a new texture
			uint32_t textureID = uploadDecodedImage(
			    decodedImages[newTextureIdx], sRGBToLoad[newTextureIdx]);
			results[i] = textureID;

			// Free decoded pixel data
			if (decodedImages[newTextureIdx].pixels) {
				stbi_image_free(decodedImages[newTextureIdx].pixels);
			}

			newTextureIdx++;
		}
	}

	auto endTime = std::chrono::high_resolution_clock::now();
	auto totalDuration = std::chrono::duration_cast<std::chrono::milliseconds>(
	    endTime - startTime);
	auto uploadDuration = std::chrono::duration_cast<std::chrono::milliseconds>(
	    endTime - decodeTime);

	std::cout << "  Uploaded in " << uploadDuration.count() << "ms"
	          << std::endl;
	std::cout << "Batch load complete: " << totalDuration.count() << "ms total"
	          << std::endl;

	return results;
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

DecodedImage TextureManager::decodeImage(const std::string& filepath) {
	DecodedImage result;
	result.filepath = filepath;

	result.pixels = stbi_load(filepath.c_str(), &result.width, &result.height,
	                          &result.channels, STBI_rgb_alpha);

	if (result.pixels) {
		result.success = true;
		result.channels = 4;  // force RGBA
	} else {
		result.success = false;
		std::cerr << "Failed to decode texture: " << filepath << std::endl;
	}

	return result;
}

uint32_t TextureManager::uploadDecodedImage(const DecodedImage& decoded,
                                            bool sRGB) {
	if (!decoded.success || !decoded.pixels) {
		return 0;
	}

	// Create texture via backend
	TextureCreateInfo createInfo;
	createInfo.pixels = decoded.pixels;
	createInfo.width = static_cast<uint32_t>(decoded.width);
	createInfo.height = static_cast<uint32_t>(decoded.height);
	createInfo.channels = 4;
	createInfo.generateMipmaps = true;
	createInfo.sRGB = sRGB;

	BackendTextureHandle backendHandle = m_backend->createTexture(createInfo);

	if (!backendHandle) {
		std::cerr << "Backend failed to create texture: " << decoded.filepath
		          << std::endl;
		return 0;
	}

	// Store metadata
	uint32_t textureID = m_nextID++;
	TextureMetadata metadata;
	metadata.filepath = decoded.filepath;
	metadata.width = createInfo.width;
	metadata.height = createInfo.height;
	metadata.backendHandle = backendHandle;

	m_textures[textureID] = metadata;
	m_pathToID[decoded.filepath] = textureID;

	return textureID;
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