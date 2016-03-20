//=====================================================
// Block.hpp
// by Andrew Socha
//=====================================================

#pragma once

#ifndef __included_Block__
#define __included_Block__

#include <vector>
#include "BlockDefinition.hpp"

typedef unsigned short BlockIndex;

const unsigned char BITMASK_BLOCK_LIGHT = 0x0F;
const unsigned char BITMASK_BLOCK_IS_SKY = 0x10;
const unsigned char BITMASK_BLOCK_LIGHT_DIRTY = 0x20;

class Chunk;
struct BlockLocation{
	Chunk* m_chunk;
	BlockIndex m_index;

	inline BlockLocation(){}
	inline BlockLocation(Chunk* chunk, BlockIndex index):m_chunk(chunk), m_index(index){}
};
typedef std::vector<BlockLocation> BlockLocations;

struct Block{
public:
	unsigned char m_type;
	unsigned char m_lightingAndFlags;

	inline Block():m_type(BT_INVALID),m_lightingAndFlags(0){}
	inline Block(unsigned char type):m_type(type),m_lightingAndFlags(0){}

	unsigned char GetLightValue() const;
	void SetLightValue(unsigned char lightValue);

	inline SoundIDs GetBreakSounds() const{ return g_blockDefinitions[m_type].m_breakSounds;}
	inline SoundIDs GetPlaceSounds() const{ return g_blockDefinitions[m_type].m_placeSounds;}
	inline SoundIDs GetWalkSounds() const{ return g_blockDefinitions[m_type].m_walkSounds;}
	
	void DirtyLighting();
	void UndirtyLighting();

	void MarkAsSky();
	void UnmarkAsSky();

	bool IsVisible() const;
	bool IsLightingDirty() const;
	bool IsSky() const;
};

///=====================================================
/// 
///=====================================================
inline unsigned char Block::GetLightValue() const{
	return m_lightingAndFlags & BITMASK_BLOCK_LIGHT;
}

///=====================================================
/// 
///=====================================================
inline void Block::SetLightValue(unsigned char lightValue){
	m_lightingAndFlags &= ~BITMASK_BLOCK_LIGHT;
	m_lightingAndFlags |= lightValue;
}

///=====================================================
/// 
///=====================================================
inline void Block::DirtyLighting(){
	m_lightingAndFlags |= BITMASK_BLOCK_LIGHT_DIRTY;
}

///=====================================================
/// 
///=====================================================
inline void Block::UndirtyLighting(){
	m_lightingAndFlags &= ~BITMASK_BLOCK_LIGHT_DIRTY;
}

///=====================================================
/// 
///=====================================================
inline bool Block::IsVisible() const{
	return g_blockDefinitions[m_type].m_isVisible;
}

///=====================================================
/// 
///=====================================================
inline void Block::MarkAsSky(){
	m_lightingAndFlags |= BITMASK_BLOCK_IS_SKY;
}

///=====================================================
/// 
///=====================================================
inline void Block::UnmarkAsSky(){
	m_lightingAndFlags &= ~BITMASK_BLOCK_IS_SKY;
}

///=====================================================
/// 
///=====================================================
inline bool Block::IsLightingDirty() const{
	return ((m_lightingAndFlags & BITMASK_BLOCK_LIGHT_DIRTY) == BITMASK_BLOCK_LIGHT_DIRTY);
}

///=====================================================
/// 
///=====================================================
inline bool Block::IsSky() const{
	return ((m_lightingAndFlags & BITMASK_BLOCK_IS_SKY) == BITMASK_BLOCK_IS_SKY);
}

#endif