#pragma once
#include "pch.h"

using Entity = uint32_t;

constexpr Entity NULL_ENTITY = std::numeric_limits<Entity>::max();

// Entity handle manipulation
namespace EntityUtil {
// Extract index from entity handle
inline uint16_t getIndex(Entity entity) {
	return static_cast<uint16_t>(entity & 0xFFFF);
}

// Extract version from entity handle
inline uint16_t getVersion(Entity entity) {
	return static_cast<uint16_t>(entity >> 16);
}

// Create entity handle from index and version
inline Entity createHandle(uint16_t index, uint16_t version) {
	return (static_cast<uint32_t>(version) << 16) | index;
}

// Maximum number of entities (16-bit index)
constexpr uint16_t MAX_ENTITIES = std::numeric_limits<uint16_t>::max();
}  // namespace EntityUtil