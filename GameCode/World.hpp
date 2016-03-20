//=====================================================
// World.hpp
// by Andrew Socha
//=====================================================

#pragma once

#ifndef __included_World__
#define __included_World__

#include "Engine/Math/AABB3D.hpp"
#include "Chunk.hpp"
class Camera;
class AnimatedTexture;
class InputSystem;

const unsigned char DAYLIGHT = 15;
const unsigned char MEDIUMLIGHT = 10;
const unsigned char MOONLIGHT = 6;

extern Vec3s g_debugPositions;
extern bool g_debugPointsEnabled;

class World{
private:
	Chunks m_activeChunks;
	BlockLocations m_dirtyBlocks;
	BlockLocations m_nextDirtyBlocksDebug;
	unsigned char m_lightLevel;
	bool m_isRunning;
	AnimatedTexture* m_textureAtlas;
	AnimatedTexture* m_skybox;
	AnimatedTexture* m_snowTexture;
	AnimatedTexture* m_rainTexture;

	Camera* m_camera;
	AABB3D m_playerBox;
	Vec3 m_playerLocalVelocity;
	bool m_playerIsRunning;
	bool m_playerIsWalking;
	bool m_playerIsFlying;
	bool m_playerIsNoClip;
	bool m_playerIsOnGround;
	bool m_playerIsInWater;
	bool m_playerIsOnIce;
	BlockType m_selectedBlockType;
	double m_countUntilNextWalkSound;
	SoundID m_splashSound;
	SoundID m_rainSound;
	SoundIDs m_thunderSounds;
	SoundIDs m_music;
	Sound* m_currentMusic;
	Sound* m_currentThunderSound;
	Sound* m_currentRainSound;
	double m_timeUntilThunder;

	void InitializeBlockDefinitions() const;

	void DirtyNonopaqueNeighbors(const BlockLocation& blockLocation, bool includingAboveBelow);
	void PlaceBlockWithRaycast(BlockType blocktype, const Raycast3DResult& raycastResult, BlockLocations& dirtyBlocksList);
	void DestroyBlockWithRaycast(const Raycast3DResult& raycastResult, BlockLocations& dirtyBlocksList);

	void ActivateNearestNeededChunk();
	void DeactivateFurthestChunk(const OpenGLRenderer* renderer);
	void ActivateChunk(const ChunkCoords& chunkCoords);
	void DeactivateChunk(const ChunkCoords& chunkCoords, const OpenGLRenderer* renderer);
	void OnChunkActivated(Chunk* chunk);
	void OnChunkDeactivated(const ChunkCoords& chunkCoords);

	bool IsChunkActive(const ChunkCoords& chunkCoords) const;

	Chunk* CreateChunkFromPerlinNoise(const ChunkCoords& chunkCoords) const;
	Chunk* CreateChunkFromFile(const ChunkCoords& chunkCoords) const;
	void SaveChunkToFile(const ChunkCoords& chunkCoords) const;

	void RenderSkybox(const OpenGLRenderer* renderer) const;
	void RenderDebugPoints(const OpenGLRenderer* renderer) const;
	void RenderBlock(const OpenGLRenderer* renderer) const;
	void RenderBlockSelectionTab(const OpenGLRenderer* renderer) const;
	void RenderChunks(const OpenGLRenderer* renderer) const;

	void PlaceOrRemoveBlockBeneathCamera();
	void PlaceOrRemoveBlockWithRaycast();
	void RenderRaycastTargetBlockOutline(const OpenGLRenderer* renderer) const;
	const Raycast3DResult Raycast3D(const WorldCoords& start, const WorldCoords& end) const;

	void UpdatePlayer(double deltaSeconds);
	void UpdatePlayerVelocityFromInput(float deltaSeconds);
	void UpdatePlayerMovementModeFromInput();
	void UpdatePlayerVelocityFromGravity(float deltaSeconds);
	const Raycast3DResult MovePlayerWithRaycast(const Vec3& totalPlayerTranslation);
	const Vec3s GetPlayerBoxContactPoints() const;
	bool MovePlayerWhenStuckInsideBlocks();

	void UpdateLighting();
	void UpdateLightingForBlock(const BlockLocation& blockLocation);

	void UpdateSoundAndMusic(double deltaSeconds);

	const BlockLocation GetBlockLocation(const BlockLocation& blockLocation, short indexOffset) const;
	unsigned char CalculateIdealLightingForBlock(const BlockLocation& blockLocation) const;
	Block& GetBlock(const BlockLocation& blockLocation) const;

	void UpdateBlockSelectionTab();

public:
	World();
	
	void Update(double deltaSeconds, const OpenGLRenderer* renderer);
	void Draw(const OpenGLRenderer* renderer) const;

	void Startup();
	void Shutdown(const OpenGLRenderer* renderer);
	inline bool IsRunning() const{return m_isRunning;}
};

///=====================================================
/// 
///=====================================================
inline Block& World::GetBlock(const BlockLocation& blockLocation) const{
	return blockLocation.m_chunk->m_blocks[blockLocation.m_index];
}

#endif