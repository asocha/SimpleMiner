//=====================================================
// Chunk.hpp
// by Andrew Socha
//=====================================================

#pragma once

#ifndef __included_Chunk__
#define __included_Chunk__

#include "Block.hpp"
#include "Engine/Renderer/OpenGLRenderer.hpp"
#include "Engine/Math/IntVec2.hpp"
#include "Engine/Math/IntVec3.hpp"
class AnimatedTexture;

const int CHUNKS_WIDE_EXPONENT = 4;
const int CHUNKS_LONG_EXPONENT = 4;
const int CHUNKS_HIGH_EXPONENT = 7;
const int BLOCKS_PER_CHUNK_X = 1 << CHUNKS_WIDE_EXPONENT;
const int BLOCKS_PER_CHUNK_Y = 1 << CHUNKS_LONG_EXPONENT;
const int BLOCKS_PER_CHUNK_Z = 1 << CHUNKS_HIGH_EXPONENT;
const int BLOCKS_PER_CHUNK = BLOCKS_PER_CHUNK_X * BLOCKS_PER_CHUNK_Y * BLOCKS_PER_CHUNK_Z;
const int BLOCKS_PER_CHUNK_LAYER = BLOCKS_PER_CHUNK_X * BLOCKS_PER_CHUNK_Y;

const int CHUNK_X_MASK = BLOCKS_PER_CHUNK_X - 1;
const int CHUNK_Y_MASK = BLOCKS_PER_CHUNK_Y - 1;
const int CHUNK_Z_MASK = BLOCKS_PER_CHUNK_Z - 1;
const int CHUNK_LAYER_MASK = BLOCKS_PER_CHUNK_LAYER - 1;

const int STEP_EAST = 1;
const int STEP_WEST = -1;
const int STEP_NORTH = BLOCKS_PER_CHUNK_X;
const int STEP_SOUTH = -BLOCKS_PER_CHUNK_X;
const int STEP_UP = BLOCKS_PER_CHUNK_LAYER;
const int STEP_DOWN = -BLOCKS_PER_CHUNK_LAYER;

const BlockIndex BLOCKINDEX_X_MASK = (BlockIndex)(BLOCKS_PER_CHUNK_X - 1);
const BlockIndex BLOCKINDEX_Y_MASK = (BlockIndex)((BLOCKS_PER_CHUNK_Y - 1) << CHUNKS_WIDE_EXPONENT);
const BlockIndex BLOCKINDEX_Z_MASK = (BlockIndex)((BLOCKS_PER_CHUNK_Z - 1) << (CHUNKS_WIDE_EXPONENT + CHUNKS_LONG_EXPONENT));

const int RLE_ENTRY_BYTES = sizeof(Block) + sizeof(BlockIndex);
const int MAX_RLE_BYTES = BLOCKS_PER_CHUNK * RLE_ENTRY_BYTES;

typedef IntVec3 LocalCoords;
typedef Vec3 WorldCoords;
typedef IntVec2 ChunkCoords;

extern Vec3s g_debugPositions;
extern bool g_debugPointsEnabled;

struct Raycast3DResult{
public:
	inline Raycast3DResult(){}

	bool m_didImpact;
	WorldCoords m_impactWorldCoords; //the corner where the white outline begins
	WorldCoords m_impactWorldCoordsMins;
	Vec3s m_impactFaceCoords;
	Vec3 m_impactSurfaceNormal;
};

class Chunk{
private:
	int m_numVertexesInVBO;
	Vertex3D_PCT_Faces m_translucentBlocksVertexFaceArray;
	static Vertex3D_PCT_Faces s_weatherVertexFaceArray;

	static unsigned char s_tempRLEBuffer[MAX_RLE_BYTES];

	void DrawBlockAtIndex(const OpenGLRenderer* renderer, BlockIndex blockIndex) const;

	unsigned char* CreateRLEBuffer(size_t& out_rleBufferSize) const;
	void PopulateFromTempRLEBuffer();
	void AppendToRLEBuffer(unsigned char blockType, unsigned short blockCount, std::vector<unsigned char>& buffer) const;
	std::string GetFilePath() const;

	void AddBlockVertexesToRenderingArray(const Block& block, BlockIndex blockIndex, Vertex3D_PCT_Faces& out_vertexFaceArray, bool useOpaqueBlocks) const;
	void AddWeatherVertexesToRenderingArray(const Block& block, BlockIndex blockIndex, Vertex3D_PCT_Faces& out_vertexFaceArray, const Vec2& camForwardNormal, bool isSnow) const;
	void PopulateVertexFaceArray(Vertex3D_PCT_Faces& out_vertexFaceArray, bool useOpaqueBlocks, bool useWeather, bool isSnow, const Vec2& camForwardNormal, const Vec3& playerPosition) const;
	void GenerateVertexArrayAndVBO(const OpenGLRenderer* renderer);
	static bool SortBlocksFurthestToNearest(const Vertex3D_PCT_Face& vertexFace1, const Vertex3D_PCT_Face& vertexFace2);

	static float CalculateWeatherAtWorldCoords(const WorldCoords& worldCoords);
	static float CalculateBiomeAtWorldCoords(const WorldCoords& worldCoords, int& out_groundHeightForColumn);

public:
	const static float SEA_LEVEL;
	const static float AVERAGE_GROUND_HEIGHT;

	Block m_blocks[BLOCKS_PER_CHUNK];
	WorldCoords m_worldCoordsMins;
	bool m_isVboDirty;
	GLuint m_vboID;
	static WorldCoords s_lastKnownCameraPosition;

	Chunk* m_chunkToNorth;
	Chunk* m_chunkToSouth;
	Chunk* m_chunkToEast;
	Chunk* m_chunkToWest;

	Chunk();

	void PopulateWithBlocks();
	
	bool IsInFrontOfCamera(const Vec3& camPosition, const Vec3& camForward) const;
	void RenderWithVAs(const OpenGLRenderer* renderer, const AnimatedTexture& texture, bool useWeather, bool isSnow, const Vec2& camForwardNormal, const Vec3& playerPosition);
	void RenderWithVBOs(const OpenGLRenderer* renderer, const AnimatedTexture& textureAtlas);
	void RenderWithGLBegin(const OpenGLRenderer* renderer, const AnimatedTexture& textureAtlas) const;
	void Update(double deltaSeconds);

	void SaveToDisk() const;
	bool LoadFromDisk();

	void PlaceBlockBeneathCoords(BlockType blocktype, const WorldCoords& worldCoords, BlockLocations& dirtyBlocksList);
	void DestroyBlockBeneathCoords(const WorldCoords& worldCoords, BlockLocations& dirtyBlocksList);

	void DirtyEastBorderNonopaqueBlocks(BlockLocations& dirtyBlocksList);
	void DirtyWestBorderNonopaqueBlocks(BlockLocations& dirtyBlocksList);
	void DirtyNorthBorderNonopaqueBlocks(BlockLocations& dirtyBlocksList);
	void DirtySouthBorderNonopaqueBlocks(BlockLocations& dirtyBlocksList);

	static const LocalCoords GetLocalCoordsAtIndex(BlockIndex blockIndex);
	static BlockIndex GetIndexAtLocalCoords(const LocalCoords& localCoords);
	static const ChunkCoords GetChunkCoordsAtWorldCoords(const WorldCoords& worldCoords);
	static const WorldCoords GetWorldCoordsAtChunkCoords(const ChunkCoords& chunkCoords);
	const WorldCoords GetWorldCoordsAtLocalCoords(const LocalCoords& localCoords) const;
	static const LocalCoords GetLocalCoordsAtWorldCoords(const WorldCoords& worldCoords);
	static BlockIndex GetIndexAtWorldCoords(const WorldCoords& worldCoords);
	const WorldCoords GetWorldCoordsAtIndex(BlockIndex blockIndex) const;

	static bool IsRainingAtWorldCoords(const WorldCoords& worldCoords);
};
typedef std::map<ChunkCoords, Chunk*> Chunks;

///=====================================================
/// 
///=====================================================
inline Chunk::Chunk()
:m_isVboDirty(true),
m_vboID(0),
m_chunkToWest(NULL),
m_chunkToSouth(NULL),
m_chunkToNorth(NULL),
m_chunkToEast(NULL),
m_numVertexesInVBO(0),
m_worldCoordsMins(0.0f, 0.0f, 0.0f){
}

///=====================================================
/// 
///=====================================================
inline const LocalCoords Chunk::GetLocalCoordsAtIndex(BlockIndex blockIndex){
	int z = blockIndex >> (CHUNKS_WIDE_EXPONENT + CHUNKS_LONG_EXPONENT);
	int y = (blockIndex & CHUNK_LAYER_MASK) >> CHUNKS_WIDE_EXPONENT;
	int x = blockIndex & CHUNK_X_MASK;
	return LocalCoords(x, y, z);
}

///=====================================================
/// 
///=====================================================
inline BlockIndex Chunk::GetIndexAtLocalCoords(const LocalCoords& localCoords){
	return (BlockIndex)(localCoords.x | (localCoords.y << CHUNKS_WIDE_EXPONENT) | (localCoords.z << (CHUNKS_WIDE_EXPONENT + CHUNKS_LONG_EXPONENT)));
}

///=====================================================
/// 
///=====================================================
inline const ChunkCoords Chunk::GetChunkCoordsAtWorldCoords(const WorldCoords& worldCoords){
	return ChunkCoords(RoundDownToInt(worldCoords.x) >> CHUNKS_WIDE_EXPONENT, RoundDownToInt(worldCoords.y) >> CHUNKS_LONG_EXPONENT);
}

///=====================================================
/// 
///=====================================================
inline const WorldCoords Chunk::GetWorldCoordsAtChunkCoords(const ChunkCoords& chunkCoords){
	return WorldCoords((float)(chunkCoords.x << CHUNKS_WIDE_EXPONENT), (float)(chunkCoords.y << CHUNKS_LONG_EXPONENT), 0.0f);
}

///=====================================================
/// 
///=====================================================
inline const WorldCoords Chunk::GetWorldCoordsAtLocalCoords(const LocalCoords& localCoords) const{
	return WorldCoords((float)localCoords.x + m_worldCoordsMins.x, (float)localCoords.y + m_worldCoordsMins.y, (float)localCoords.z + m_worldCoordsMins.z);
}

///=====================================================
/// 
///=====================================================
inline const LocalCoords Chunk::GetLocalCoordsAtWorldCoords(const WorldCoords& worldCoords){
	int x = RoundDownToInt(worldCoords.x) & CHUNK_X_MASK;
	int y = RoundDownToInt(worldCoords.y) & CHUNK_Y_MASK;
	int z = RoundDownToInt(worldCoords.z) & CHUNK_Z_MASK;
	return LocalCoords(x, y, z);
}

///=====================================================
/// 
///=====================================================
inline BlockIndex Chunk::GetIndexAtWorldCoords(const WorldCoords& worldCoords){
	int worldZ = RoundDownToInt(worldCoords.z);
	if (worldZ >= BLOCKS_PER_CHUNK_Z)
		return (BlockIndex)(-1); //return an invalid index

	int localX = RoundDownToInt(worldCoords.x) & CHUNK_X_MASK;
	int localY = RoundDownToInt(worldCoords.y) & CHUNK_Y_MASK;
	int localZ = worldZ & CHUNK_Z_MASK;
	return (BlockIndex)(localX | (localY << CHUNKS_WIDE_EXPONENT) | (localZ << (CHUNKS_WIDE_EXPONENT + CHUNKS_LONG_EXPONENT)));
}

///=====================================================
/// 
///=====================================================
inline const WorldCoords Chunk::GetWorldCoordsAtIndex(BlockIndex blockIndex) const{
	int localZ = blockIndex >> (CHUNKS_WIDE_EXPONENT + CHUNKS_LONG_EXPONENT);
	int localY = (blockIndex & CHUNK_LAYER_MASK) >> CHUNKS_WIDE_EXPONENT;
	int localX = blockIndex & CHUNK_X_MASK;
	return WorldCoords((float)localX + m_worldCoordsMins.x, (float)localY + m_worldCoordsMins.y, (float)localZ + m_worldCoordsMins.z);
}

#endif