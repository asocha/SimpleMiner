//=====================================================
// BlockDefinition.hpp
// by Andrew Socha
//=====================================================

#pragma once

#ifndef __included_BlockDefinition__
#define __included_BlockDefinition__

#include "Engine/Math/Vec2.hpp"
#include "Engine/Sound/Sound.hpp"

enum BlockType{
	BT_AIR,
	BT_GRASS,
	BT_DIRT,
	BT_STONE,
	BT_WATER,
	BT_SAND,
	BT_GLOWSTONE,
	BT_ICE,
	BT_SNOW,
	BLOCK_TYPE_COUNT,
	BT_INVALID
};

struct BlockDefinition{
public:
	BlockType m_type;
	
	Vec2 m_topTexCoordsMins;
	Vec2 m_bottomTexCoordsMins;
	Vec2 m_sideTexCoordsMins;

	bool m_isSolid;
	bool m_isOpaque;
	bool m_isVisible;
	bool m_fallsWithGravity;

	unsigned char m_inherentLightValue; //0-15

	SoundIDs m_walkSounds;
	SoundIDs m_placeSounds;
	SoundIDs m_breakSounds;

	inline BlockDefinition(){}
};

extern BlockDefinition g_blockDefinitions[BLOCK_TYPE_COUNT];

#endif