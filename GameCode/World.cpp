//=====================================================
// World.cpp
// by Andrew Socha
//=====================================================

#include "World.hpp"
#include "Engine/Time/Time.hpp"
#include "Engine/Sound/SoundSystem.hpp"
#include "BlockDefinition.hpp"
#include "Engine/Renderer/AnimatedTexture.hpp"
#include "Engine/Renderer/Camera.hpp"
#include "Engine/Input/InputSystem.hpp"

const int INNER_VISIBILITY_DISTANCE = 15;
const int INNER_DISTANCE_THERMOSTAT_QUALIFICATION = (INNER_VISIBILITY_DISTANCE) * (INNER_VISIBILITY_DISTANCE) + 1;
const int OUTER_VISIBILITY_DISTANCE = INNER_VISIBILITY_DISTANCE + 1;
const int OUTER_DISTANCE_THERMOSTAT_QUALIFICATION = (OUTER_VISIBILITY_DISTANCE) * (OUTER_VISIBILITY_DISTANCE);

const Vec2 MOUSE_RESET_POSITION(400.0f, 300.0f);

const float PLAYER_HEIGHT = 1.85f;
const float PLAYER_WIDTH = 0.6f;
const float CAMERA_HEIGHT = 1.62f;

///=====================================================
/// 
///=====================================================
World::World()
:m_isRunning(true),
m_textureAtlas(0),
m_skybox(0),
m_camera(0),
m_playerIsRunning(false),
m_playerIsFlying(false),
m_playerIsWalking(true),
m_playerIsNoClip(false),
m_playerIsOnGround(false),
m_playerIsOnIce(false),
m_playerIsInWater(false),
m_countUntilNextWalkSound(0.0),
m_rainSound(0),
m_currentMusic(NULL),
m_currentThunderSound(NULL),
m_currentRainSound(NULL),
m_snowTexture(NULL),
m_rainTexture(NULL),
m_splashSound(-1),
m_timeUntilThunder(GetRandomDoubleInRange(2.0, 5.0)),
m_lightLevel(DAYLIGHT),
m_selectedBlockType((BlockType)1),
m_playerBox(Vec3(0.0f, 0.0f, Chunk::SEA_LEVEL), Vec3(PLAYER_WIDTH, PLAYER_WIDTH, Chunk::SEA_LEVEL + PLAYER_HEIGHT)),
m_playerLocalVelocity(0.0f, 0.0f, 0.0f){
}

///=====================================================
/// 
///=====================================================
void World::Startup(){
	m_textureAtlas = AnimatedTexture::CreateOrGetAnimatedTexture("Data/Images/SimpleMinerAtlas.png", 1024, 32, 32);
	m_skybox = AnimatedTexture::CreateOrGetAnimatedTexture("Data/Images/skybox_texture.png", 12, 1024, 1024);
	m_rainTexture = AnimatedTexture::CreateOrGetAnimatedTexture("Data/Images/Rain.png", 2, 32, 32);
	m_snowTexture = AnimatedTexture::CreateOrGetAnimatedTexture("Data/Images/Snow.png", 2, 32, 32);

	InitializeBlockDefinitions();
	m_splashSound = s_theSoundSystem->LoadStreamingSound("Data/Sounds/splash.ogg", 1);
	m_rainSound = s_theSoundSystem->LoadStreamingSound("Data/Sounds/rain-01.ogg", 1);
	m_thunderSounds.push_back(s_theSoundSystem->LoadStreamingSound("Data/Sounds/thunder1.ogg", 1));
	m_thunderSounds.push_back(s_theSoundSystem->LoadStreamingSound("Data/Sounds/thunder2.ogg", 1));
	m_thunderSounds.push_back(s_theSoundSystem->LoadStreamingSound("Data/Sounds/thunder3.ogg", 1));
	m_music.push_back(s_theSoundSystem->LoadStreamingSound("Data/Sounds/hal1.ogg", 1));
	m_music.push_back(s_theSoundSystem->LoadStreamingSound("Data/Sounds/hal3.ogg", 1));
	s_theSoundSystem->ReadySounds();

	m_camera = new Camera(Vec3(PLAYER_WIDTH * 0.5f, PLAYER_WIDTH * 0.5f, Chunk::SEA_LEVEL + CAMERA_HEIGHT), EulerAngles(0.0f, 0.0f, 0.0f));
	s_theInputSystem->SetMousePosition(MOUSE_RESET_POSITION);

	m_dirtyBlocks.reserve(10000);
}

///=====================================================
/// 
///=====================================================
void World::Shutdown(const OpenGLRenderer* renderer){
	delete m_camera;

	m_isRunning = false;

	Chunks::iterator mapIter;
	while (!m_activeChunks.empty()){
		mapIter = m_activeChunks.begin();
		DeactivateChunk(mapIter->first, renderer);
	}
}

///=====================================================
/// 
///=====================================================
void World::Update(double deltaSeconds, const OpenGLRenderer* renderer){
	if (s_theInputSystem->IsKeyDown('X') && s_theInputSystem->DidStateJustChange('X')){
		g_debugPointsEnabled = !g_debugPointsEnabled;
		g_debugPositions.clear();
		if (!g_debugPointsEnabled){
			m_dirtyBlocks = m_nextDirtyBlocksDebug;
			m_nextDirtyBlocksDebug.clear();
		}
		else{
			g_debugPositions.reserve(10000);
			m_nextDirtyBlocksDebug.reserve(10000);
		}
	}
	
	UpdateBlockSelectionTab();

	ActivateNearestNeededChunk();
	DeactivateFurthestChunk(renderer);

	if (m_camera){
		UpdatePlayer(deltaSeconds);
	}

	PlaceOrRemoveBlockWithRaycast();

	if (g_debugPointsEnabled && s_theInputSystem->IsKeyDown('C') && s_theInputSystem->DidStateJustChange('C')){
		m_dirtyBlocks = m_nextDirtyBlocksDebug;
		m_nextDirtyBlocksDebug.clear();
		m_nextDirtyBlocksDebug.reserve(10000);
		g_debugPositions.clear();
		g_debugPositions.reserve(10000);
	}

	UpdateLighting();
	UpdateSoundAndMusic(deltaSeconds);

	for (Chunks::iterator chunkIter = m_activeChunks.begin(); chunkIter != m_activeChunks.end(); ++chunkIter){
		Chunk* chunk = chunkIter->second;
		chunk->Update(deltaSeconds);
	}
}

///=====================================================
/// 
///=====================================================
void World::Draw(const OpenGLRenderer* renderer) const{
	renderer->ApplyCameraTransform(*m_camera);

	RenderSkybox(renderer);

	RenderBlockSelectionTab(renderer);
	RenderRaycastTargetBlockOutline(renderer);

	if (g_debugPointsEnabled){
		RenderDebugPoints(renderer);
	}

	RenderChunks(renderer);


	renderer->SetOrthographicView();
	renderer->SetDepthTest(true);
	if (m_playerIsInWater)
		renderer->DrawOverlay(RGBA(0.0f, 0.0f, 0.5f, min(0.5f + 0.05f * (Chunk::SEA_LEVEL - m_camera->m_position.z), 0.75f)));

	else if (m_timeUntilThunder > 0.0 && m_timeUntilThunder <= 0.75)
		renderer->DrawOverlay(RGBA((float)m_timeUntilThunder * (4.0f / 3.0f), (float)m_timeUntilThunder * (4.0f / 3.0f), (float)m_timeUntilThunder * (4.0f / 3.0f), 0.4f + 0.6f * (float)m_timeUntilThunder));
		//lightning that transitions into the normal rain overlay

	else if (Chunk::IsRainingAtWorldCoords(m_camera->m_position))
		renderer->DrawOverlay(RGBA(0.0f, 0.0f, 0.0f, 0.4f));

	renderer->DrawCrosshair(2.0f, 15.0f);
}

///=====================================================
/// 
///=====================================================
void World::RenderChunks(const OpenGLRenderer* renderer) const{
	const Vec3 camForward = m_camera->GetCameraForwardNormal();
	static Vec3 pausedCamForward;
	static Vec3 pausedCamPosition;
	static bool frustumPaused = false;
	if (s_theInputSystem->IsKeyDown('P') && s_theInputSystem->DidStateJustChange('P')){
		frustumPaused = !frustumPaused;
		if (frustumPaused){
			pausedCamPosition = m_camera->m_position;
			pausedCamForward = camForward;
		}
	}


	//Sort chunks from closest to furthest from the player
	std::vector<Chunk*> chunkSorter;
	int xOffset = 0;
	const ChunkCoords playerChunkCoords = Chunk::GetChunkCoordsAtWorldCoords(m_camera->m_position);
	unsigned int targetSize = m_activeChunks.size();
	while (chunkSorter.size() != targetSize){
		int chunkY1 = playerChunkCoords.y;
		int chunkY2 = playerChunkCoords.y;
		for (int chunkX = playerChunkCoords.x - xOffset; chunkX <= playerChunkCoords.x + xOffset; ++chunkX){
			Chunks::const_iterator chunkIter = m_activeChunks.find(ChunkCoords(chunkX, chunkY1));
			if (chunkIter != m_activeChunks.end()){
				if (chunkIter->second->IsInFrontOfCamera(frustumPaused ? pausedCamPosition : m_camera->m_position, frustumPaused ? pausedCamForward : camForward))
					chunkSorter.push_back(chunkIter->second);
				else
					--targetSize;
			}

			if (chunkY1 != chunkY2){
				Chunks::const_iterator chunkIter = m_activeChunks.find(ChunkCoords(chunkX, chunkY2));
				if (chunkIter != m_activeChunks.end()){
					if (chunkIter->second->IsInFrontOfCamera(frustumPaused ? pausedCamPosition : m_camera->m_position, frustumPaused ? pausedCamForward : camForward))
						chunkSorter.push_back(chunkIter->second);
					else
						--targetSize;
				}
			}

			if (chunkX < playerChunkCoords.x){
				--chunkY1;
				++chunkY2;
			}
			else{
				++chunkY1;
				--chunkY2;
			}
		}
		++xOffset;
	}

	const Vec3 camForwardNormal3D = m_camera->GetCameraForwardNormal();
	Vec2 camForwardNormal2D(camForwardNormal3D);
	camForwardNormal2D.Normalize();
	//render opaque blocks closest to furthest
	for (std::vector<Chunk*>::const_iterator chunkIter = chunkSorter.begin(); chunkIter != chunkSorter.end(); ++chunkIter){
		Chunk* chunk = *chunkIter;

		//render weather too
		chunk->RenderWithVAs(renderer, *m_snowTexture, true, true, camForwardNormal2D, m_camera->m_position); //render snow
		chunk->RenderWithVAs(renderer, *m_rainTexture, true, false, camForwardNormal2D, m_camera->m_position); //render rain

		chunk->RenderWithVBOs(renderer, *m_textureAtlas);
	}

	//render translucent blocks furthest to closest
	Chunk::s_lastKnownCameraPosition = m_camera->m_position;
	for (std::vector<Chunk*>::const_iterator chunkIter = chunkSorter.end() - 1; ; --chunkIter){
		Chunk* chunk = *chunkIter;
		chunk->RenderWithVAs(renderer, *m_textureAtlas, false, false, Vec2(), Vec3()); //translucent

		if (chunkIter == chunkSorter.begin())
			return;
	}
}

///=====================================================
/// 
///=====================================================
void World::ActivateNearestNeededChunk(){
	int shortestCandidateDistSquared = INNER_DISTANCE_THERMOSTAT_QUALIFICATION;
	ChunkCoords chunkCoordsToActivate;
	const ChunkCoords playerCoords = Chunk::GetChunkCoordsAtWorldCoords(m_camera->m_position);
	for (int x = playerCoords.x - OUTER_VISIBILITY_DISTANCE; x <= playerCoords.x + OUTER_VISIBILITY_DISTANCE; ++x){
		for (int y = playerCoords.y - OUTER_VISIBILITY_DISTANCE; y <= playerCoords.y + OUTER_VISIBILITY_DISTANCE; ++y){
			const ChunkCoords chunkCoords(x, y);
			int distanceSquared = CalcDistanceSquared(chunkCoords, playerCoords);
			if (distanceSquared < shortestCandidateDistSquared && !IsChunkActive(chunkCoords)){
				shortestCandidateDistSquared = distanceSquared;
				chunkCoordsToActivate = chunkCoords;
			}
		}
	}

	if (shortestCandidateDistSquared < INNER_DISTANCE_THERMOSTAT_QUALIFICATION){
		ActivateChunk(chunkCoordsToActivate);
	}
}

///=====================================================
/// 
///=====================================================
bool World::IsChunkActive(const ChunkCoords& chunkCoords) const{
	for (Chunks::const_iterator chunkIter = m_activeChunks.begin(); chunkIter != m_activeChunks.end(); ++chunkIter){
		if (chunkCoords == chunkIter->first){
			return true;
		}
	}

	return false;
}

///=====================================================
/// 
///=====================================================
void World::ActivateChunk(const ChunkCoords& chunkCoords){
	Chunk* newChunk = CreateChunkFromFile(chunkCoords);

	if (newChunk == NULL){
		newChunk = CreateChunkFromPerlinNoise(chunkCoords);
	}

	const ChunkCoords chunkCoordsNorth(chunkCoords.x, chunkCoords.y + 1);
	Chunks::iterator northChunk = m_activeChunks.find(chunkCoordsNorth);
	if (northChunk != m_activeChunks.end()){
		newChunk->m_chunkToNorth = northChunk->second;
		northChunk->second->m_chunkToSouth = newChunk;
	}
	else{
		newChunk->m_chunkToNorth = NULL;
	}

	const ChunkCoords chunkCoordsSouth(chunkCoords.x, chunkCoords.y - 1);
	Chunks::iterator southChunk = m_activeChunks.find(chunkCoordsSouth);
	if (southChunk != m_activeChunks.end()){
		newChunk->m_chunkToSouth = southChunk->second;
		southChunk->second->m_chunkToNorth = newChunk;
	}
	else{
		newChunk->m_chunkToSouth = NULL;
	}

	const ChunkCoords chunkCoordsEast(chunkCoords.x + 1, chunkCoords.y);
	Chunks::iterator eastChunk = m_activeChunks.find(chunkCoordsEast);
	if (eastChunk != m_activeChunks.end()){
		newChunk->m_chunkToEast = eastChunk->second;
		eastChunk->second->m_chunkToWest = newChunk;
	}
	else{
		newChunk->m_chunkToEast = NULL;
	}

	const ChunkCoords chunkCoordsWest(chunkCoords.x - 1, chunkCoords.y);
	Chunks::iterator westChunk = m_activeChunks.find(chunkCoordsWest);
	if (westChunk != m_activeChunks.end()){
		newChunk->m_chunkToWest = westChunk->second;
		westChunk->second->m_chunkToEast = newChunk;
	}
	else{
		newChunk->m_chunkToWest = NULL;
	}

	m_activeChunks[chunkCoords] = newChunk;
	OnChunkActivated(newChunk);
}

///=====================================================
/// 
///=====================================================
Chunk* World::CreateChunkFromPerlinNoise(const ChunkCoords& chunkCoords) const{
	Chunk* chunk = new Chunk();
	chunk->m_worldCoordsMins = Chunk::GetWorldCoordsAtChunkCoords(chunkCoords);
	chunk->PopulateWithBlocks();
	return chunk;
}

///=====================================================
/// 
///=====================================================
Chunk* World::CreateChunkFromFile(const ChunkCoords& chunkCoords) const{
	Chunk* chunk = new Chunk();
	chunk->m_worldCoordsMins = Chunk::GetWorldCoordsAtChunkCoords(chunkCoords);
	bool didLoad = chunk->LoadFromDisk();
	if (!didLoad){
		delete chunk;
		chunk = NULL;
	}

	return chunk;
}

///=====================================================
/// 
///=====================================================
void World::OnChunkActivated(Chunk* chunk){
	for (int column = 1; column <= BLOCKS_PER_CHUNK_LAYER; ++column){
		bool endedSky = false;
		for (BlockIndex index = (BlockIndex)(BLOCKS_PER_CHUNK - column); index < BLOCKS_PER_CHUNK; index -= BLOCKS_PER_CHUNK_LAYER){
			Block& block = chunk->m_blocks[index];
			
			if (!endedSky && block.m_type == BT_AIR){ //set up lighting for sky and dirty its neighbors
				block.MarkAsSky();
				block.SetLightValue(m_lightLevel);

				BlockLocation blockLocation(chunk, index);
				DirtyNonopaqueNeighbors(blockLocation, false);
			}
			else if (block.GetLightValue() != 0){ //dirty lighting around glowstones and any other light-omitting blocks
				endedSky = true;
				BlockLocation blockLocation(chunk, index);
				DirtyNonopaqueNeighbors(blockLocation, true);
			}
			else if (!g_blockDefinitions[block.m_type].m_isOpaque && !block.IsLightingDirty()){ //mark non-opaque blocks (water) as dirty
				endedSky = true;
				block.DirtyLighting();
				BlockLocation blockLocation(chunk, index);
				if (g_debugPointsEnabled){
					g_debugPositions.push_back(blockLocation.m_chunk->GetWorldCoordsAtIndex(blockLocation.m_index));
					m_nextDirtyBlocksDebug.push_back(blockLocation);
				}
				else
					m_dirtyBlocks.push_back(blockLocation);
			}
			else{
				endedSky = true;
			}
		}
	}
}

///=====================================================
/// 
///=====================================================
void World::DeactivateFurthestChunk(const OpenGLRenderer* renderer){
	int furthestCandidateDistSquared = OUTER_DISTANCE_THERMOSTAT_QUALIFICATION;
	ChunkCoords chunkCoordsToDeactivate;
	const ChunkCoords playerCoords = Chunk::GetChunkCoordsAtWorldCoords(m_camera->m_position);
	for (Chunks::iterator chunkIter = m_activeChunks.begin(); chunkIter != m_activeChunks.end(); ++chunkIter){
		const ChunkCoords& chunkCoords = chunkIter->first;
		int distanceSquared = CalcDistanceSquared(chunkCoords, playerCoords);
		if (distanceSquared > furthestCandidateDistSquared){
			furthestCandidateDistSquared = distanceSquared;
			chunkCoordsToDeactivate = chunkCoords;
		}
	}

	if (furthestCandidateDistSquared > OUTER_DISTANCE_THERMOSTAT_QUALIFICATION){
		DeactivateChunk(chunkCoordsToDeactivate, renderer);
	}
}

///=====================================================
/// 
///=====================================================
void World::DeactivateChunk(const ChunkCoords& chunkCoords, const OpenGLRenderer* renderer){
	SaveChunkToFile(chunkCoords);

	OnChunkDeactivated(chunkCoords);

	renderer->DeleteBuffer(&(m_activeChunks[chunkCoords]->m_vboID));

	if (m_isRunning){ //don't update lighting if the user is quitting the game
		const ChunkCoords chunkCoordsNorth(chunkCoords.x, chunkCoords.y + 1);
		Chunks::iterator northChunk = m_activeChunks.find(chunkCoordsNorth);
		if (northChunk != m_activeChunks.end()){
			northChunk->second->m_chunkToSouth = NULL;
			northChunk->second->DirtySouthBorderNonopaqueBlocks(g_debugPointsEnabled ? m_nextDirtyBlocksDebug : m_dirtyBlocks);
		}

		const ChunkCoords chunkCoordsSouth(chunkCoords.x, chunkCoords.y - 1);
		Chunks::iterator southChunk = m_activeChunks.find(chunkCoordsSouth);
		if (southChunk != m_activeChunks.end()){
			southChunk->second->m_chunkToNorth = NULL;
			southChunk->second->DirtyNorthBorderNonopaqueBlocks(g_debugPointsEnabled ? m_nextDirtyBlocksDebug : m_dirtyBlocks);
		}

		const ChunkCoords chunkCoordsEast(chunkCoords.x + 1, chunkCoords.y);
		Chunks::iterator eastChunk = m_activeChunks.find(chunkCoordsEast);
		if (eastChunk != m_activeChunks.end()){
			eastChunk->second->m_chunkToWest = NULL;
			eastChunk->second->DirtyWestBorderNonopaqueBlocks(g_debugPointsEnabled ? m_nextDirtyBlocksDebug : m_dirtyBlocks);
		}

		const ChunkCoords chunkCoordsWest(chunkCoords.x - 1, chunkCoords.y);
		Chunks::iterator westChunk = m_activeChunks.find(chunkCoordsWest);
		if (westChunk != m_activeChunks.end()){
			westChunk->second->m_chunkToEast = NULL;
			westChunk->second->DirtyEastBorderNonopaqueBlocks(g_debugPointsEnabled ? m_nextDirtyBlocksDebug : m_dirtyBlocks);
		}
	}

	m_activeChunks[chunkCoords] = NULL;
	delete m_activeChunks[chunkCoords];
	m_activeChunks.erase(chunkCoords);
}

///=====================================================
/// 
///=====================================================
void World::SaveChunkToFile(const ChunkCoords& chunkCoords) const{
	m_activeChunks.at(chunkCoords)->SaveToDisk();
}

///=====================================================
/// 
///=====================================================
void World::OnChunkDeactivated(const ChunkCoords& /*chunkCoords*/){

}

///=====================================================
/// FOR DEBUGGING
///=====================================================
void World::RenderBlock(const OpenGLRenderer* renderer) const{
	static Vec3s bottomVertices;
	static Vec3s topVertices;
	static Vec3s northVertices;
	static Vec3s southVertices;
	static Vec3s eastVertices;
	static Vec3s westVertices;
	if (bottomVertices.empty()){
		bottomVertices.push_back(Vec3(1.0f, 0.0f, 0.0f));
		bottomVertices.push_back(Vec3(0.0f, 0.0f, 0.0f));
		bottomVertices.push_back(Vec3(0.0f, 1.0f, 0.0f));
		bottomVertices.push_back(Vec3(1.0f, 1.0f, 0.0f));

		topVertices.push_back(Vec3(0.0f, 0.0f, 1.0f));
		topVertices.push_back(Vec3(1.0f, 0.0f, 1.0f));
		topVertices.push_back(Vec3(1.0f, 1.0f, 1.0f));
		topVertices.push_back(Vec3(0.0f, 1.0f, 1.0f));

		northVertices.push_back(Vec3(1.0f, 1.0f, 0.0f));
		northVertices.push_back(Vec3(0.0f, 1.0f, 0.0f));
		northVertices.push_back(Vec3(0.0f, 1.0f, 1.0f));
		northVertices.push_back(Vec3(1.0f, 1.0f, 1.0f));

		southVertices.push_back(Vec3(0.0f, 0.0f, 0.0f));
		southVertices.push_back(Vec3(1.0f, 0.0f, 0.0f));
		southVertices.push_back(Vec3(1.0f, 0.0f, 1.0f));
		southVertices.push_back(Vec3(0.0f, 0.0f, 1.0f));

		eastVertices.push_back(Vec3(1.0f, 0.0f, 0.0f));
		eastVertices.push_back(Vec3(1.0f, 1.0f, 0.0f));
		eastVertices.push_back(Vec3(1.0f, 1.0f, 1.0f));
		eastVertices.push_back(Vec3(1.0f, 0.0f, 1.0f));

		westVertices.push_back(Vec3(0.0f, 1.0f, 0.0f));
		westVertices.push_back(Vec3(0.0f, 0.0f, 0.0f));
		westVertices.push_back(Vec3(0.0f, 0.0f, 1.0f));
		westVertices.push_back(Vec3(0.0f, 1.0f, 1.0f));
	}

	renderer->DrawTexturedQuad(*m_textureAtlas, bottomVertices, m_textureAtlas->CalcTextureCoordinatesAtPercentComplete(0.611328125));
	renderer->DrawTexturedQuad(*m_textureAtlas, topVertices, m_textureAtlas->CalcTextureCoordinatesAtPercentComplete(0.678710938));
	const Vec2s sideTextureCoords = m_textureAtlas->CalcTextureCoordinatesAtPercentComplete(0.612304688);
	renderer->DrawTexturedQuad(*m_textureAtlas, northVertices, sideTextureCoords);
	renderer->DrawTexturedQuad(*m_textureAtlas, southVertices, sideTextureCoords);
	renderer->DrawTexturedQuad(*m_textureAtlas, eastVertices, sideTextureCoords);
	renderer->DrawTexturedQuad(*m_textureAtlas, westVertices, sideTextureCoords);
}

///=====================================================
/// 
///=====================================================
void World::InitializeBlockDefinitions() const{
	g_blockDefinitions[BT_AIR].m_bottomTexCoordsMins = m_textureAtlas->CalcMinimumTextureCoordinatesAtSpriteNumber(0);
	g_blockDefinitions[BT_AIR].m_topTexCoordsMins = g_blockDefinitions[BT_AIR].m_bottomTexCoordsMins;
	g_blockDefinitions[BT_AIR].m_sideTexCoordsMins = g_blockDefinitions[BT_AIR].m_bottomTexCoordsMins;
	g_blockDefinitions[BT_AIR].m_fallsWithGravity = false;
	g_blockDefinitions[BT_AIR].m_isOpaque = false;
	g_blockDefinitions[BT_AIR].m_isSolid = false;
	g_blockDefinitions[BT_AIR].m_isVisible = false;
	g_blockDefinitions[BT_AIR].m_inherentLightValue = 0;
	g_blockDefinitions[BT_AIR].m_type = BT_AIR;

	g_blockDefinitions[BT_GRASS].m_bottomTexCoordsMins = m_textureAtlas->CalcMinimumTextureCoordinatesAtSpriteNumber(626);
	g_blockDefinitions[BT_GRASS].m_topTexCoordsMins = m_textureAtlas->CalcMinimumTextureCoordinatesAtSpriteNumber(695);
	g_blockDefinitions[BT_GRASS].m_sideTexCoordsMins = m_textureAtlas->CalcMinimumTextureCoordinatesAtSpriteNumber(627);
	g_blockDefinitions[BT_GRASS].m_fallsWithGravity = false;
	g_blockDefinitions[BT_GRASS].m_isOpaque = true;
	g_blockDefinitions[BT_GRASS].m_isSolid = true;
	g_blockDefinitions[BT_GRASS].m_isVisible = true;
	g_blockDefinitions[BT_GRASS].m_inherentLightValue = 0;
	g_blockDefinitions[BT_GRASS].m_type = BT_GRASS;
	g_blockDefinitions[BT_GRASS].m_placeSounds.push_back(s_theSoundSystem->LoadStreamingSound("Data/Sounds/gravel2.ogg", 2));
	g_blockDefinitions[BT_GRASS].m_placeSounds.push_back(s_theSoundSystem->LoadStreamingSound("Data/Sounds/gravel3.ogg", 2));
	g_blockDefinitions[BT_GRASS].m_placeSounds.push_back(s_theSoundSystem->LoadStreamingSound("Data/Sounds/gravel4.ogg", 2));
	g_blockDefinitions[BT_GRASS].m_breakSounds = g_blockDefinitions[BT_GRASS].m_placeSounds;
	g_blockDefinitions[BT_GRASS].m_walkSounds.push_back(s_theSoundSystem->LoadStreamingSound("Data/Sounds/grass1.ogg", 2));
	g_blockDefinitions[BT_GRASS].m_walkSounds.push_back(s_theSoundSystem->LoadStreamingSound("Data/Sounds/grass2.ogg", 2));
	g_blockDefinitions[BT_GRASS].m_walkSounds.push_back(s_theSoundSystem->LoadStreamingSound("Data/Sounds/grass3.ogg", 2));
	g_blockDefinitions[BT_GRASS].m_walkSounds.push_back(s_theSoundSystem->LoadStreamingSound("Data/Sounds/grass4.ogg", 2));
	g_blockDefinitions[BT_GRASS].m_walkSounds.push_back(s_theSoundSystem->LoadStreamingSound("Data/Sounds/grass5.ogg", 2));
	g_blockDefinitions[BT_GRASS].m_walkSounds.push_back(s_theSoundSystem->LoadStreamingSound("Data/Sounds/grass6.ogg", 2));

	g_blockDefinitions[BT_DIRT].m_bottomTexCoordsMins = m_textureAtlas->CalcMinimumTextureCoordinatesAtSpriteNumber(626);
	g_blockDefinitions[BT_DIRT].m_topTexCoordsMins = g_blockDefinitions[BT_DIRT].m_bottomTexCoordsMins;
	g_blockDefinitions[BT_DIRT].m_sideTexCoordsMins = g_blockDefinitions[BT_DIRT].m_bottomTexCoordsMins;
	g_blockDefinitions[BT_DIRT].m_fallsWithGravity = false;
	g_blockDefinitions[BT_DIRT].m_isOpaque = true;
	g_blockDefinitions[BT_DIRT].m_isSolid = true;
	g_blockDefinitions[BT_DIRT].m_isVisible = true;
	g_blockDefinitions[BT_DIRT].m_inherentLightValue = 0;
	g_blockDefinitions[BT_DIRT].m_type = BT_DIRT;
	g_blockDefinitions[BT_DIRT].m_breakSounds = g_blockDefinitions[BT_GRASS].m_breakSounds;
	g_blockDefinitions[BT_DIRT].m_placeSounds = g_blockDefinitions[BT_GRASS].m_placeSounds;
	g_blockDefinitions[BT_DIRT].m_walkSounds = g_blockDefinitions[BT_GRASS].m_placeSounds;
	g_blockDefinitions[BT_DIRT].m_walkSounds.push_back(s_theSoundSystem->LoadStreamingSound("Data/Sounds/gravel1.ogg", 2));

	g_blockDefinitions[BT_STONE].m_bottomTexCoordsMins = m_textureAtlas->CalcMinimumTextureCoordinatesAtSpriteNumber(624);
	g_blockDefinitions[BT_STONE].m_topTexCoordsMins = g_blockDefinitions[BT_STONE].m_bottomTexCoordsMins;
	g_blockDefinitions[BT_STONE].m_sideTexCoordsMins = g_blockDefinitions[BT_STONE].m_bottomTexCoordsMins;
	g_blockDefinitions[BT_STONE].m_fallsWithGravity = false;
	g_blockDefinitions[BT_STONE].m_isOpaque = true;
	g_blockDefinitions[BT_STONE].m_isSolid = true;
	g_blockDefinitions[BT_STONE].m_isVisible = true;
	g_blockDefinitions[BT_STONE].m_inherentLightValue = 0;
	g_blockDefinitions[BT_STONE].m_type = BT_STONE;
	g_blockDefinitions[BT_STONE].m_walkSounds.push_back(s_theSoundSystem->LoadStreamingSound("Data/Sounds/stone1.ogg", 2));
	g_blockDefinitions[BT_STONE].m_walkSounds.push_back(s_theSoundSystem->LoadStreamingSound("Data/Sounds/stone2.ogg", 2));
	g_blockDefinitions[BT_STONE].m_walkSounds.push_back(s_theSoundSystem->LoadStreamingSound("Data/Sounds/stone3.ogg", 2));
	g_blockDefinitions[BT_STONE].m_walkSounds.push_back(s_theSoundSystem->LoadStreamingSound("Data/Sounds/stone4.ogg", 2));
	g_blockDefinitions[BT_STONE].m_walkSounds.push_back(s_theSoundSystem->LoadStreamingSound("Data/Sounds/stone5.ogg", 2));
	g_blockDefinitions[BT_STONE].m_walkSounds.push_back(s_theSoundSystem->LoadStreamingSound("Data/Sounds/stone6.ogg", 2));
	g_blockDefinitions[BT_STONE].m_breakSounds = g_blockDefinitions[BT_STONE].m_walkSounds;
	g_blockDefinitions[BT_STONE].m_placeSounds = g_blockDefinitions[BT_STONE].m_walkSounds;

	g_blockDefinitions[BT_WATER].m_bottomTexCoordsMins = m_textureAtlas->CalcMinimumTextureCoordinatesAtSpriteNumber(1022);
	g_blockDefinitions[BT_WATER].m_topTexCoordsMins = g_blockDefinitions[BT_WATER].m_bottomTexCoordsMins;
	g_blockDefinitions[BT_WATER].m_sideTexCoordsMins = g_blockDefinitions[BT_WATER].m_bottomTexCoordsMins;
	g_blockDefinitions[BT_WATER].m_fallsWithGravity = true;
	g_blockDefinitions[BT_WATER].m_isOpaque = false;
	g_blockDefinitions[BT_WATER].m_isSolid = false;
	g_blockDefinitions[BT_WATER].m_isVisible = true;
	g_blockDefinitions[BT_WATER].m_inherentLightValue = 0;
	g_blockDefinitions[BT_WATER].m_type = BT_WATER;
	g_blockDefinitions[BT_WATER].m_walkSounds.push_back(s_theSoundSystem->LoadStreamingSound("Data/Sounds/swim1.ogg", 1));
	g_blockDefinitions[BT_WATER].m_walkSounds.push_back(s_theSoundSystem->LoadStreamingSound("Data/Sounds/swim2.ogg", 1));
	g_blockDefinitions[BT_WATER].m_walkSounds.push_back(s_theSoundSystem->LoadStreamingSound("Data/Sounds/swim3.ogg", 1));
	g_blockDefinitions[BT_WATER].m_walkSounds.push_back(s_theSoundSystem->LoadStreamingSound("Data/Sounds/swim4.ogg", 1));
	g_blockDefinitions[BT_WATER].m_breakSounds = g_blockDefinitions[BT_WATER].m_walkSounds;
	g_blockDefinitions[BT_WATER].m_placeSounds = g_blockDefinitions[BT_WATER].m_walkSounds;

	g_blockDefinitions[BT_SAND].m_bottomTexCoordsMins = m_textureAtlas->CalcMinimumTextureCoordinatesAtSpriteNumber(658);
	g_blockDefinitions[BT_SAND].m_topTexCoordsMins = g_blockDefinitions[BT_SAND].m_bottomTexCoordsMins;
	g_blockDefinitions[BT_SAND].m_sideTexCoordsMins = g_blockDefinitions[BT_SAND].m_bottomTexCoordsMins;
	g_blockDefinitions[BT_SAND].m_fallsWithGravity = false;
	g_blockDefinitions[BT_SAND].m_isOpaque = true;
	g_blockDefinitions[BT_SAND].m_isSolid = true;
	g_blockDefinitions[BT_SAND].m_isVisible = true;
	g_blockDefinitions[BT_SAND].m_inherentLightValue = 0;
	g_blockDefinitions[BT_SAND].m_type = BT_SAND;
	g_blockDefinitions[BT_SAND].m_walkSounds.push_back(s_theSoundSystem->LoadStreamingSound("Data/Sounds/sand1.ogg", 2));
	g_blockDefinitions[BT_SAND].m_walkSounds.push_back(s_theSoundSystem->LoadStreamingSound("Data/Sounds/sand2.ogg", 2));
	g_blockDefinitions[BT_SAND].m_walkSounds.push_back(s_theSoundSystem->LoadStreamingSound("Data/Sounds/sand3.ogg", 2));
	g_blockDefinitions[BT_SAND].m_walkSounds.push_back(s_theSoundSystem->LoadStreamingSound("Data/Sounds/sand4.ogg", 2));
	g_blockDefinitions[BT_SAND].m_walkSounds.push_back(s_theSoundSystem->LoadStreamingSound("Data/Sounds/sand5.ogg", 2));
	g_blockDefinitions[BT_SAND].m_placeSounds = g_blockDefinitions[BT_SAND].m_walkSounds;
	g_blockDefinitions[BT_SAND].m_breakSounds = g_blockDefinitions[BT_SAND].m_walkSounds;

	g_blockDefinitions[BT_GLOWSTONE].m_bottomTexCoordsMins = m_textureAtlas->CalcMinimumTextureCoordinatesAtSpriteNumber(201);
	g_blockDefinitions[BT_GLOWSTONE].m_topTexCoordsMins = g_blockDefinitions[BT_GLOWSTONE].m_bottomTexCoordsMins;
	g_blockDefinitions[BT_GLOWSTONE].m_sideTexCoordsMins = g_blockDefinitions[BT_GLOWSTONE].m_bottomTexCoordsMins;
	g_blockDefinitions[BT_GLOWSTONE].m_fallsWithGravity = false;
	g_blockDefinitions[BT_GLOWSTONE].m_isOpaque = true;
	g_blockDefinitions[BT_GLOWSTONE].m_isSolid = true;
	g_blockDefinitions[BT_GLOWSTONE].m_isVisible = true;
	g_blockDefinitions[BT_GLOWSTONE].m_inherentLightValue = 14;
	g_blockDefinitions[BT_GLOWSTONE].m_type = BT_GLOWSTONE;
	g_blockDefinitions[BT_GLOWSTONE].m_breakSounds = g_blockDefinitions[BT_STONE].m_breakSounds;
	g_blockDefinitions[BT_GLOWSTONE].m_placeSounds = g_blockDefinitions[BT_STONE].m_placeSounds;
	g_blockDefinitions[BT_GLOWSTONE].m_walkSounds = g_blockDefinitions[BT_STONE].m_walkSounds;

	g_blockDefinitions[BT_ICE].m_bottomTexCoordsMins = m_textureAtlas->CalcMinimumTextureCoordinatesAtSpriteNumber(755);
	g_blockDefinitions[BT_ICE].m_topTexCoordsMins = g_blockDefinitions[BT_ICE].m_bottomTexCoordsMins;
	g_blockDefinitions[BT_ICE].m_sideTexCoordsMins = g_blockDefinitions[BT_ICE].m_bottomTexCoordsMins;
	g_blockDefinitions[BT_ICE].m_fallsWithGravity = false;
	g_blockDefinitions[BT_ICE].m_isOpaque = false;
	g_blockDefinitions[BT_ICE].m_isSolid = true;
	g_blockDefinitions[BT_ICE].m_isVisible = true;
	g_blockDefinitions[BT_ICE].m_inherentLightValue = 0;
	g_blockDefinitions[BT_ICE].m_type = BT_ICE;
	g_blockDefinitions[BT_ICE].m_breakSounds = g_blockDefinitions[BT_STONE].m_breakSounds;
	g_blockDefinitions[BT_ICE].m_placeSounds = g_blockDefinitions[BT_STONE].m_placeSounds;
	g_blockDefinitions[BT_ICE].m_walkSounds = g_blockDefinitions[BT_STONE].m_walkSounds;

	g_blockDefinitions[BT_SNOW].m_bottomTexCoordsMins = m_textureAtlas->CalcMinimumTextureCoordinatesAtSpriteNumber(626);
	g_blockDefinitions[BT_SNOW].m_topTexCoordsMins = m_textureAtlas->CalcMinimumTextureCoordinatesAtSpriteNumber(754);
	g_blockDefinitions[BT_SNOW].m_sideTexCoordsMins = m_textureAtlas->CalcMinimumTextureCoordinatesAtSpriteNumber(756);
	g_blockDefinitions[BT_SNOW].m_fallsWithGravity = false;
	g_blockDefinitions[BT_SNOW].m_isOpaque = true;
	g_blockDefinitions[BT_SNOW].m_isSolid = true;
	g_blockDefinitions[BT_SNOW].m_isVisible = true;
	g_blockDefinitions[BT_SNOW].m_inherentLightValue = 0;
	g_blockDefinitions[BT_SNOW].m_type = BT_SNOW;
	g_blockDefinitions[BT_SNOW].m_breakSounds = g_blockDefinitions[BT_GRASS].m_breakSounds;
	g_blockDefinitions[BT_SNOW].m_placeSounds = g_blockDefinitions[BT_GRASS].m_placeSounds;
	g_blockDefinitions[BT_SNOW].m_walkSounds.push_back(s_theSoundSystem->LoadStreamingSound("Data/Sounds/snow1.ogg", 2));
	g_blockDefinitions[BT_SNOW].m_walkSounds.push_back(s_theSoundSystem->LoadStreamingSound("Data/Sounds/snow2.ogg", 2));
	g_blockDefinitions[BT_SNOW].m_walkSounds.push_back(s_theSoundSystem->LoadStreamingSound("Data/Sounds/snow3.ogg", 2));
	g_blockDefinitions[BT_SNOW].m_walkSounds.push_back(s_theSoundSystem->LoadStreamingSound("Data/Sounds/snow4.ogg", 2));
}

///=====================================================
/// 
///=====================================================
void World::UpdatePlayerMovementModeFromInput(){
	if (s_theInputSystem->IsKeyDown('E') && s_theInputSystem->DidStateJustChange('E') && !m_playerIsFlying){
		m_playerIsFlying = true;
		m_playerIsWalking = false;
		m_playerIsNoClip = false;
		m_playerIsInWater = false;
		m_playerLocalVelocity = Vec3(0.0f, 0.0f, 0.0f);
	}
	else if (s_theInputSystem->IsKeyDown('F') && s_theInputSystem->DidStateJustChange('F') && !m_playerIsWalking){
		m_playerIsFlying = false;
		m_playerIsWalking = true;
		m_playerIsNoClip = false;
		m_playerIsRunning = false;
		m_playerLocalVelocity = Vec3(0.0f, 0.0f, 0.0f);
	}
	else if (s_theInputSystem->IsKeyDown('R') && s_theInputSystem->DidStateJustChange('R') && !m_playerIsNoClip){
		m_playerIsFlying = false;
		m_playerIsWalking = false;
		m_playerIsNoClip = true;
		m_playerIsInWater = false;
		m_playerLocalVelocity = Vec3(0.0f, 0.0f, 0.0f);
	}
	else if (m_playerIsWalking && s_theInputSystem->IsKeyDown(VK_SHIFT) && s_theInputSystem->DidStateJustChange(VK_SHIFT)){
		m_playerIsRunning = !m_playerIsRunning;
	}
}

///=====================================================
/// 
///=====================================================
void World::UpdatePlayerVelocityFromGravity(float deltaSeconds){
	if (m_playerLocalVelocity.z < 0.0f)
		m_playerIsOnGround = false;

	//check if player is in water
	const ChunkCoords chunkCoords = Chunk::GetChunkCoordsAtWorldCoords(m_camera->m_position);
	Chunks::const_iterator chunkIter = m_activeChunks.find(chunkCoords);
	if (chunkIter != m_activeChunks.end()){
		Chunk* chunk = chunkIter->second;
		BlockIndex index = chunk->GetIndexAtWorldCoords(m_camera->m_position);
		if (chunk->m_blocks[index].m_type == BT_WATER){ //player is in water - reduced gravity
			if (!m_playerIsInWater){ //played just entered water from nonwater
				s_theSoundSystem->PlaySound(m_splashSound, 0, 0.4f);
				m_playerIsInWater = true;
			}
			else{
				if (m_countUntilNextWalkSound <= 0.0){
					const SoundIDs& swimSounds = g_blockDefinitions[BT_WATER].m_walkSounds;
					s_theSoundSystem->PlayRandomSound(swimSounds, 0, 0.05f);
					m_countUntilNextWalkSound = GetRandomDoubleInRange(4.0, 5.0);
				}
			}
			m_playerIsOnGround = false;
			m_playerLocalVelocity.z -= 1.6f * deltaSeconds;
			if (m_playerLocalVelocity.z < -0.25f)
				m_playerLocalVelocity.z = -0.25f;
		}
		else if (m_playerIsInWater){ //player moved from water to nonwater - give him an upward speed boost
			m_playerLocalVelocity.z += 5.5f;
			m_playerIsInWater = false;
		}
		else{ //player is not in water - normal gravity
			m_playerLocalVelocity.z -= 9.0f * deltaSeconds;
			if (m_playerLocalVelocity.z < -10.0f)
				m_playerLocalVelocity.z = -10.0f;
		}
	}
}

///=====================================================
/// 
///=====================================================
void World::UpdatePlayer(double deltaSeconds){
	const static float MOVE_SPEED = 4.22f;
	const static float RUN_SPEED = 5.77f;
	const static float FLY_SPEED = 9.09f;
	const static float SWIM_SPEED = 3.0f;
	const static float NO_CLIP_SPEED = 20.0f;

	UpdatePlayerMovementModeFromInput();

	if (m_playerIsWalking)
		UpdatePlayerVelocityFromGravity((float)deltaSeconds);

	UpdatePlayerVelocityFromInput((float)deltaSeconds);

	//determine speed
	float currentSpeed;
	if (m_playerIsInWater)
		currentSpeed = SWIM_SPEED;
	else if (m_playerIsWalking){
		if (m_playerIsRunning){
			currentSpeed = RUN_SPEED;
		}
		else{
			currentSpeed = MOVE_SPEED;
		}
	}
	else if (m_playerIsFlying){
		currentSpeed = FLY_SPEED;
	}
	else/* if (m_playerIsNoClip)*/{
		currentSpeed = NO_CLIP_SPEED;
	}

	//Determine total player translation this frame
	float yawRadians = ConvertDegreesToRadians(m_camera->m_orientation.yawDegreesAboutZ);
	const Vec3 camFwdXY(cos(yawRadians), sin(yawRadians), 0.0f);
	const Vec3 camLeftXY(-camFwdXY.y, camFwdXY.x, 0.0f);

	Vec3 totalPlayerTranslation = (camFwdXY * m_playerLocalVelocity.x) + (camLeftXY * m_playerLocalVelocity.y);

	float velocityMagnitude = AsymptoticAdd(abs(m_playerLocalVelocity.x), abs(m_playerLocalVelocity.y));
	if (!m_playerIsWalking || m_playerIsInWater){
		totalPlayerTranslation.z = m_playerLocalVelocity.z;
		velocityMagnitude = AsymptoticAdd(velocityMagnitude, abs(m_playerLocalVelocity.z));
	}

	totalPlayerTranslation.SetLength(velocityMagnitude * (float)deltaSeconds * currentSpeed);

	if (m_playerIsWalking && !m_playerIsInWater){
		totalPlayerTranslation.z = m_playerLocalVelocity.z * (float)deltaSeconds;
	}

	//Move player
	if (!m_playerIsNoClip){
		if (totalPlayerTranslation.x != 0.0f || totalPlayerTranslation.y != 0.0f || (totalPlayerTranslation.z != 0.0f && m_playerIsInWater))
			m_countUntilNextWalkSound -= deltaSeconds * currentSpeed;
		MovePlayerWithRaycast(totalPlayerTranslation);
	}
	else{
		m_playerBox.Translate(totalPlayerTranslation);
		m_camera->m_position += totalPlayerTranslation;
	}

	//Mouse Camera Controls
	const static float DEGREES_PER_MOUSE_DELTA = 0.04f;

	const Vec2 mousePosition = s_theInputSystem->GetMousePosition();
	const Vec2 mouseMovementLastFrame = mousePosition - MOUSE_RESET_POSITION;
	s_theInputSystem->SetMousePosition(MOUSE_RESET_POSITION);

	m_camera->m_orientation.yawDegreesAboutZ -= mouseMovementLastFrame.x * DEGREES_PER_MOUSE_DELTA;
	m_camera->m_orientation.pitchDegreesAboutY += mouseMovementLastFrame.y * DEGREES_PER_MOUSE_DELTA;
	if (m_camera->m_orientation.pitchDegreesAboutY > 89.0f)
		m_camera->m_orientation.pitchDegreesAboutY = 89.0f;
	else if (m_camera->m_orientation.pitchDegreesAboutY < -89.0f)
		m_camera->m_orientation.pitchDegreesAboutY = -89.0f;
}

///=====================================================
/// 
///=====================================================
const Raycast3DResult World::MovePlayerWithRaycast(const Vec3& totalPlayerTranslation){
	const static float RAYCAST_INCREMENT = 0.01f;
	Raycast3DResult result;
	result.m_didImpact = false;

	if ((totalPlayerTranslation.x == 0.0f) && (totalPlayerTranslation.y == 0.0f) && (totalPlayerTranslation.z == 0.0f))
		return result;

	Vec3 raycastIncrement = totalPlayerTranslation * RAYCAST_INCREMENT;

	Chunk* chunk;
	BlockIndex index;

	bool playerIsWithinWorld = MovePlayerWhenStuckInsideBlocks();
	if (!playerIsWithinWorld)
		return result;
	
	for (float t = 0.0f; t < 1.0f; t += RAYCAST_INCREMENT){
		m_playerBox.Translate(raycastIncrement);
		m_camera->m_position += raycastIncrement;
		const Vec3s playerBoxContactPoints = GetPlayerBoxContactPoints();

		for (Vec3s::const_iterator cornerIter = playerBoxContactPoints.begin(); cornerIter != playerBoxContactPoints.end(); ++cornerIter){
			const ChunkCoords chunkCoords = Chunk::GetChunkCoordsAtWorldCoords(*cornerIter);
			Chunks::const_iterator chunkIter = m_activeChunks.find(chunkCoords);
			if (chunkIter == m_activeChunks.end())
				return result;
			chunk = chunkIter->second;
			index = chunk->GetIndexAtWorldCoords(*cornerIter);
			const Block& block = chunk->m_blocks[index];

			if (g_blockDefinitions[block.m_type].m_isSolid){
				result.m_didImpact = true;

				if (RoundDownToInt(cornerIter->z) != RoundDownToInt(cornerIter->z - raycastIncrement.z)){
					m_playerBox.Translate(-raycastIncrement);
					m_camera->m_position -= raycastIncrement;
					m_playerLocalVelocity.z = 0.0f;
					m_playerIsOnGround = true;
					if (block.m_type == BT_ICE)
						m_playerIsOnIce = true;
					else
						m_playerIsOnIce = false;

					if (raycastIncrement.x == 0.0f && raycastIncrement.y == 0.0f)
						return result;
					raycastIncrement.z = 0.0f;
					break;
				}
				else if (RoundDownToInt(cornerIter->x) != RoundDownToInt(cornerIter->x - raycastIncrement.x)){
					m_playerBox.Translate(-raycastIncrement);
					m_camera->m_position -= raycastIncrement;

					float cosCameraYaw = cos(ConvertDegreesToRadians(m_camera->m_orientation.yawDegreesAboutZ));
					float sinCameraYaw = sin(ConvertDegreesToRadians(m_camera->m_orientation.yawDegreesAboutZ));
					float playerWorldVelocityY = (m_playerLocalVelocity.x * sinCameraYaw) + (m_playerLocalVelocity.y * cosCameraYaw);
					m_playerLocalVelocity.x = playerWorldVelocityY * sinCameraYaw;
					m_playerLocalVelocity.y = playerWorldVelocityY * cosCameraYaw;

					if (raycastIncrement.z == 0.0f && raycastIncrement.y == 0.0f)
						return result;
					raycastIncrement.x = 0.0f;
					break;
				}
				else if (RoundDownToInt(cornerIter->y) != RoundDownToInt(cornerIter->y - raycastIncrement.y)){
					m_playerBox.Translate(-raycastIncrement);
					m_camera->m_position -= raycastIncrement;
					
					float cosCameraYaw = cos(ConvertDegreesToRadians(m_camera->m_orientation.yawDegreesAboutZ));
					float sinCameraYaw = sin(ConvertDegreesToRadians(m_camera->m_orientation.yawDegreesAboutZ));
					float playerWorldVelocityX = (m_playerLocalVelocity.x * cosCameraYaw) + (m_playerLocalVelocity.y * -sinCameraYaw);
					m_playerLocalVelocity.x = playerWorldVelocityX * cosCameraYaw;
					m_playerLocalVelocity.y = playerWorldVelocityX * -sinCameraYaw;

					if (raycastIncrement.x == 0.0f && raycastIncrement.z == 0.0f)
						return result;
					raycastIncrement.y = 0.0f;
					break;
				}
			}
		}
	}
	return result;
}

///=====================================================
/// 
///=====================================================
void World::UpdatePlayerVelocityFromInput(float deltaSeconds){
	const static float ACCELERATION_REGULAR = 5.0f;
	const static float ACCELERATION_ICE = 1.4f;
	const static float ACCELERATION_AIR = 1.0f;
	const static float JUMP_VELOCITY = 4.8f;

	float acceleration = deltaSeconds;
	if (m_playerIsOnIce)
		acceleration *= ACCELERATION_ICE;
	else if (m_playerIsWalking && !m_playerIsOnGround)
		acceleration *= ACCELERATION_AIR;
	else
		acceleration *= ACCELERATION_REGULAR;

	//determine velocity
	if (s_theInputSystem->IsKeyDown('W') || s_theInputSystem->IsKeyDown(VK_UP)){
		m_playerLocalVelocity.x += acceleration;
		if (m_playerLocalVelocity.x > 1.0f)
			m_playerLocalVelocity.x = 1.0f;
	}
	else if (s_theInputSystem->IsKeyDown('S') || s_theInputSystem->IsKeyDown(VK_DOWN)){
		m_playerLocalVelocity.x -= acceleration;
		if (m_playerLocalVelocity.x < -1.0f)
			m_playerLocalVelocity.x = -1.0f;
	}
	else{
		if (m_playerLocalVelocity.x > 0.0f){
			m_playerLocalVelocity.x -= acceleration;
			if (m_playerLocalVelocity.x < 0.0f)
				m_playerLocalVelocity.x = 0.0f;
		}
		else{
			m_playerLocalVelocity.x += acceleration;
			if (m_playerLocalVelocity.x > 0.0f)
				m_playerLocalVelocity.x = 0.0f;
		}
	}

	if (s_theInputSystem->IsKeyDown('A') || s_theInputSystem->IsKeyDown(VK_LEFT)){
		m_playerLocalVelocity.y += acceleration;
		if (m_playerLocalVelocity.y > 1.0f)
			m_playerLocalVelocity.y = 1.0f;
	}
	else if (s_theInputSystem->IsKeyDown('D') || s_theInputSystem->IsKeyDown(VK_RIGHT)){
		m_playerLocalVelocity.y -= acceleration;
		if (m_playerLocalVelocity.y < -1.0f)
			m_playerLocalVelocity.y = -1.0f;
	}
	else{
		if (m_playerLocalVelocity.y > 0.0f){
			m_playerLocalVelocity.y -= acceleration;
			if (m_playerLocalVelocity.y < 0.0f)
				m_playerLocalVelocity.y = 0.0f;
		}
		else{
			m_playerLocalVelocity.y += acceleration;
			if (m_playerLocalVelocity.y > 0.0f)
				m_playerLocalVelocity.y = 0.0f;
		}
	}

	if (s_theInputSystem->IsKeyDown(VK_SPACE)){
		if (m_playerIsWalking && !m_playerIsInWater){
			if (m_playerIsOnGround && s_theInputSystem->DidStateJustChange(VK_SPACE)){
				m_playerIsOnGround = false;
				m_playerLocalVelocity.z += JUMP_VELOCITY;
			}
		}
		else{
			m_playerLocalVelocity.z += acceleration;
			if (m_playerIsInWater)
				m_playerLocalVelocity.z += acceleration;
			if (m_playerLocalVelocity.z > 1.0f)
				m_playerLocalVelocity.z = 1.0f;
		}
	}
	else if (s_theInputSystem->IsKeyDown('Z') && (!m_playerIsWalking || m_playerIsInWater)){
		m_playerLocalVelocity.z -= acceleration;
		if (m_playerLocalVelocity.z < -1.0f)
			m_playerLocalVelocity.z = -1.0f;
	}
	else if (!m_playerIsWalking || m_playerIsInWater){
		if (m_playerLocalVelocity.z > 0.0f){
			m_playerLocalVelocity.z -= acceleration;
			if (m_playerLocalVelocity.z < 0.0f)
				m_playerLocalVelocity.z = 0.0f;
		}
		else if (m_playerLocalVelocity.z < 0.0f){
			m_playerLocalVelocity.z += acceleration;
			if (m_playerLocalVelocity.z > 0.0f)
				m_playerLocalVelocity.z = 0.0f;
		}
	}
}

///=====================================================
/// 
///=====================================================
void World::PlaceOrRemoveBlockBeneathCamera(){
	if (s_theInputSystem->DidStateJustChange('K') && s_theInputSystem->IsKeyDown('K')){
		//destroy block
		const ChunkCoords chunkCoords = Chunk::GetChunkCoordsAtWorldCoords(m_camera->m_position);
		Chunks::iterator chunkIter = m_activeChunks.find(chunkCoords);
		if (chunkIter != m_activeChunks.end())
			chunkIter->second->DestroyBlockBeneathCoords(m_camera->m_position, g_debugPointsEnabled ? m_nextDirtyBlocksDebug : m_dirtyBlocks);
	}
	else if (s_theInputSystem->DidStateJustChange('P') && s_theInputSystem->IsKeyDown('P')){
		//place block
		const ChunkCoords chunkCoords = Chunk::GetChunkCoordsAtWorldCoords(m_camera->m_position);
		Chunks::iterator chunkIter = m_activeChunks.find(chunkCoords);
		if (chunkIter != m_activeChunks.end())
			chunkIter->second->PlaceBlockBeneathCoords(m_selectedBlockType, m_camera->m_position, g_debugPointsEnabled ? m_nextDirtyBlocksDebug : m_dirtyBlocks);
	}
}

///=====================================================
/// 
///=====================================================
void World::UpdateLighting(){
	while (!m_dirtyBlocks.empty()){
		const BlockLocation blockLocation = m_dirtyBlocks.back();
		m_dirtyBlocks.pop_back();
		if (blockLocation.m_chunk){
			UpdateLightingForBlock(blockLocation);
			blockLocation.m_chunk->m_isVboDirty = true;
		}
	}
}

///=====================================================
/// 
///=====================================================
void World::UpdateLightingForBlock(const BlockLocation& blockLocation){
	unsigned char idealLight = CalculateIdealLightingForBlock(blockLocation);
	Block& block = GetBlock(blockLocation);
	if (block.GetLightValue() != idealLight){
		block.SetLightValue(idealLight);
		DirtyNonopaqueNeighbors(blockLocation, true);
	}
	block.UndirtyLighting();
}

///=====================================================
/// 
///=====================================================
unsigned char World::CalculateIdealLightingForBlock(const BlockLocation& blockLocation) const{
	Block& block = GetBlock(blockLocation);
	unsigned char maxAdjacentLighting = g_blockDefinitions[block.m_type].m_inherentLightValue + 1; //+1 to cancel out the -1 at the end

	const BlockLocation locationAbove = GetBlockLocation(blockLocation, STEP_UP);
	if (locationAbove.m_chunk){
		const Block& blockAbove = GetBlock(locationAbove);
		unsigned char blockAboveLighting = blockAbove.GetLightValue();
		if (blockAboveLighting > maxAdjacentLighting)
			maxAdjacentLighting = blockAboveLighting;
	}

	const BlockLocation locationBelow = GetBlockLocation(blockLocation, STEP_DOWN);
	if (locationBelow.m_chunk){
		const Block& blockBelow = GetBlock(locationBelow);
		unsigned char blockBelowLighting = blockBelow.GetLightValue();
		if (blockBelowLighting > maxAdjacentLighting)
			maxAdjacentLighting = blockBelowLighting;
	}

	const BlockLocation locationToNorth = GetBlockLocation(blockLocation, STEP_NORTH);
	if (locationToNorth.m_chunk){
		const Block& blockToNorth = GetBlock(locationToNorth);
		unsigned char blockToNorthLighting = blockToNorth.GetLightValue();
		if (blockToNorthLighting > maxAdjacentLighting)
			maxAdjacentLighting = blockToNorthLighting;
	}

	const BlockLocation locationToSouth = GetBlockLocation(blockLocation, STEP_SOUTH);
	if (locationToSouth.m_chunk){
		const Block& blockToSouth = GetBlock(locationToSouth);
		unsigned char blockToSouthLighting = blockToSouth.GetLightValue();
		if (blockToSouthLighting > maxAdjacentLighting)
			maxAdjacentLighting = blockToSouthLighting;
	}

	const BlockLocation locationToEast = GetBlockLocation(blockLocation, STEP_EAST);
	if (locationToEast.m_chunk){
		const Block& blockToEast = GetBlock(locationToEast);
		unsigned char blockToEastLighting = blockToEast.GetLightValue();
		if (blockToEastLighting > maxAdjacentLighting)
			maxAdjacentLighting = blockToEastLighting;
	}

	const BlockLocation locationToWest = GetBlockLocation(blockLocation, STEP_WEST);
	if (locationToWest.m_chunk){
		const Block& blockToWest = GetBlock(locationToWest);
		unsigned char blockToWestLighting = blockToWest.GetLightValue();
		if (blockToWestLighting > maxAdjacentLighting)
			maxAdjacentLighting = blockToWestLighting;
	}

	if (block.IsSky() && maxAdjacentLighting == m_lightLevel)
		return maxAdjacentLighting;

	return maxAdjacentLighting - 1;
}

///=====================================================
/// 
///=====================================================
void World::DirtyNonopaqueNeighbors(const BlockLocation& blockLocation, bool includingAboveBelow){
	if (includingAboveBelow){
		const BlockLocation locationAbove = GetBlockLocation(blockLocation, STEP_UP);
		if (locationAbove.m_chunk){
			locationAbove.m_chunk->m_isVboDirty = true;
			Block& blockAbove = GetBlock(locationAbove);
			if (!g_blockDefinitions[blockAbove.m_type].m_isOpaque && !blockAbove.IsLightingDirty()){
				blockAbove.DirtyLighting();
				if (g_debugPointsEnabled){
					g_debugPositions.push_back(locationAbove.m_chunk->GetWorldCoordsAtIndex(locationAbove.m_index));
					m_nextDirtyBlocksDebug.push_back(locationAbove);
				}
				else
					m_dirtyBlocks.push_back(locationAbove);
			}
		}

		const BlockLocation locationBelow = GetBlockLocation(blockLocation, STEP_DOWN);
		if (locationBelow.m_chunk){
			locationBelow.m_chunk->m_isVboDirty = true;
			Block& blockBelow = GetBlock(locationBelow);
			if (!g_blockDefinitions[blockBelow.m_type].m_isOpaque && !blockBelow.IsLightingDirty()){
				blockBelow.DirtyLighting();
				if (g_debugPointsEnabled){
					g_debugPositions.push_back(locationBelow.m_chunk->GetWorldCoordsAtIndex(locationBelow.m_index));
					m_nextDirtyBlocksDebug.push_back(locationBelow);
				}
				else
					m_dirtyBlocks.push_back(locationBelow);
			}
		}
	}

	const BlockLocation locationToNorth = GetBlockLocation(blockLocation, STEP_NORTH);
	if (locationToNorth.m_chunk){
		locationToNorth.m_chunk->m_isVboDirty = true;
		Block& blockToNorth = GetBlock(locationToNorth);
		if (!g_blockDefinitions[blockToNorth.m_type].m_isOpaque && !blockToNorth.IsLightingDirty()){
			blockToNorth.DirtyLighting();
			if (g_debugPointsEnabled){
				g_debugPositions.push_back(locationToNorth.m_chunk->GetWorldCoordsAtIndex(locationToNorth.m_index));
				m_nextDirtyBlocksDebug.push_back(locationToNorth);
			}
			else
				m_dirtyBlocks.push_back(locationToNorth);
		}
	}

	const BlockLocation locationToSouth = GetBlockLocation(blockLocation, STEP_SOUTH);
	if (locationToSouth.m_chunk){
		locationToSouth.m_chunk->m_isVboDirty = true;
		Block& blockToSouth = GetBlock(locationToSouth);
		if (!g_blockDefinitions[blockToSouth.m_type].m_isOpaque && !blockToSouth.IsLightingDirty()){
			blockToSouth.DirtyLighting();
			if (g_debugPointsEnabled){
				g_debugPositions.push_back(locationToSouth.m_chunk->GetWorldCoordsAtIndex(locationToSouth.m_index));
				m_nextDirtyBlocksDebug.push_back(locationToSouth);
			}
			else
				m_dirtyBlocks.push_back(locationToSouth);
		}
	}

	const BlockLocation locationToEast = GetBlockLocation(blockLocation, STEP_EAST);
	if (locationToEast.m_chunk){
		locationToEast.m_chunk->m_isVboDirty = true;
		Block& blockToEast = GetBlock(locationToEast);
		if (!g_blockDefinitions[blockToEast.m_type].m_isOpaque && !blockToEast.IsLightingDirty()){
			blockToEast.DirtyLighting();
			if (g_debugPointsEnabled){
				g_debugPositions.push_back(locationToEast.m_chunk->GetWorldCoordsAtIndex(locationToEast.m_index));
				m_nextDirtyBlocksDebug.push_back(locationToEast);
			}
			else
				m_dirtyBlocks.push_back(locationToEast);
		}
	}

	const BlockLocation locationToWest = GetBlockLocation(blockLocation, STEP_WEST);
	if (locationToWest.m_chunk){
		locationToWest.m_chunk->m_isVboDirty = true;
		Block& blockToWest = GetBlock(locationToWest);
		if (!g_blockDefinitions[blockToWest.m_type].m_isOpaque && !blockToWest.IsLightingDirty()){
			blockToWest.DirtyLighting();
			if (g_debugPointsEnabled){
				g_debugPositions.push_back(locationToWest.m_chunk->GetWorldCoordsAtIndex(locationToWest.m_index));
				m_nextDirtyBlocksDebug.push_back(locationToWest);
			}
			else
				m_dirtyBlocks.push_back(locationToWest);
		}
	}
}

///=====================================================
/// 
///=====================================================
void World::RenderDebugPoints(const OpenGLRenderer* renderer) const{
	double currentTime = GetCurrentSeconds();
	double currentColor = 0.5 + (0.5 * sin(currentTime));
	renderer->PushMatrix();
	renderer->SetColor(currentColor, 0.0, currentColor);
	renderer->SetPointSize(5.0f);
	renderer->SetModelViewTranslation(Vec3(0.5f, 0.5f, 0.5f));

	renderer->BeginPoints();
	for (Vec3s::const_iterator pointIter = g_debugPositions.begin(); pointIter != g_debugPositions.end(); ++pointIter){
		renderer->Vertex3f(*pointIter);
	}
	renderer->End();

	renderer->SetPointSize(3.0f);
	renderer->SetDepthTest(false);

	renderer->BeginPoints();
	for (Vec3s::const_iterator pointIter = g_debugPositions.begin(); pointIter != g_debugPositions.end(); ++pointIter){
		renderer->Vertex3f(*pointIter);
	}
	renderer->End();

	renderer->SetDepthTest(true);

	renderer->PopMatrix();
}

///=====================================================
/// 
///=====================================================
void World::RenderBlockSelectionTab(const OpenGLRenderer* renderer) const{
	const static Vec2 defaultSquareCoordinates[] = {Vec2(-1.0f, -1.0f), Vec2(1.0f, -1.0f), Vec2(1.0f, 1.0f), Vec2(-1.0f, 1.0f)};
	const Vec2s DEFAULT_SQUARE_COORDINATES(defaultSquareCoordinates, defaultSquareCoordinates + 4);
	const static float TEX_COORD_SIZE_PER_TILE = 1.0f / 32.0f;
	
	renderer->PushMatrix();
	renderer->SetOrthographicView();
	renderer->SetModelViewScale(40.0f, 40.0f);
	renderer->SetModelViewTranslation(13.0f, 2.0f, 0.0f);
	for (int blockType = 1; blockType < BLOCK_TYPE_COUNT; ++blockType){
		Vec2s textureCoords;
		textureCoords.push_back(g_blockDefinitions[blockType].m_sideTexCoordsMins + Vec2(0.0f, TEX_COORD_SIZE_PER_TILE));
		textureCoords.push_back(g_blockDefinitions[blockType].m_sideTexCoordsMins + Vec2(TEX_COORD_SIZE_PER_TILE, TEX_COORD_SIZE_PER_TILE));
		textureCoords.push_back(g_blockDefinitions[blockType].m_sideTexCoordsMins + Vec2(TEX_COORD_SIZE_PER_TILE, 0.0f));
		textureCoords.push_back(g_blockDefinitions[blockType].m_sideTexCoordsMins);
		if (blockType == m_selectedBlockType)
			renderer->DrawTexturedQuad(*m_textureAtlas, DEFAULT_SQUARE_COORDINATES, textureCoords, RGBA::WHITE);
		else
			renderer->DrawTexturedQuad(*m_textureAtlas, DEFAULT_SQUARE_COORDINATES, textureCoords, RGBA::GRAY);
		renderer->SetModelViewTranslation(2.0f, 0.0f, 0.0f);
	}
	renderer->PopMatrix();
}

///=====================================================
/// 
///=====================================================
void World::UpdateBlockSelectionTab(){
	for (int blockType = 1; blockType < BLOCK_TYPE_COUNT; ++blockType){
		if (s_theInputSystem->IsKeyDown(blockType + 48) && s_theInputSystem->DidStateJustChange(blockType + 48)){ //add 48 to convert to ASCII character
			m_selectedBlockType = (BlockType)blockType;
			return;
		}
	}
	if (s_theInputSystem->MouseWheelWentDown()){
		if (m_selectedBlockType == 1)
			m_selectedBlockType = (BlockType)(BLOCK_TYPE_COUNT - 1);
		else
			m_selectedBlockType = (BlockType)(m_selectedBlockType - 1);
	}
	else if (s_theInputSystem->MouseWheelWentUp()){
		if (m_selectedBlockType == (BLOCK_TYPE_COUNT - 1))
			m_selectedBlockType = (BlockType)(1);
		else
			m_selectedBlockType = (BlockType)(m_selectedBlockType + 1);
	}
}

///=====================================================
/// 
///=====================================================
void World::PlaceOrRemoveBlockWithRaycast(){
	if (s_theInputSystem->GetLeftMouseButtonDown()){
		//destroy block
		const Raycast3DResult raycastResult = Raycast3D(m_camera->m_position, m_camera->m_position + (8.0f * m_camera->GetCameraForwardNormal()));
		if (raycastResult.m_didImpact){
			DestroyBlockWithRaycast(raycastResult, g_debugPointsEnabled ? m_nextDirtyBlocksDebug : m_dirtyBlocks);
		}
	}
	else if (s_theInputSystem->GetRightMouseButtonDown()){
		//place block
		const Raycast3DResult raycastResult = Raycast3D(m_camera->m_position, m_camera->m_position + (8.0f * m_camera->GetCameraForwardNormal()));
		if (raycastResult.m_didImpact){
			PlaceBlockWithRaycast(m_selectedBlockType, raycastResult, g_debugPointsEnabled ? m_nextDirtyBlocksDebug : m_dirtyBlocks);
		}
	}
}

///=====================================================
/// 
///=====================================================
void World::RenderRaycastTargetBlockOutline(const OpenGLRenderer* renderer) const{
	const Raycast3DResult result = Raycast3D(m_camera->m_position, m_camera->m_position + (8.0f * m_camera->GetCameraForwardNormal()));
	if (result.m_didImpact){
		renderer->DrawPolygon(result.m_impactFaceCoords);
	}
}

///=====================================================
/// 
///=====================================================
const Raycast3DResult World::Raycast3D(const WorldCoords& start, const WorldCoords& end) const{
	const static float RAYCAST_INCREMENT = 0.001f;
	Raycast3DResult result;
	result.m_didImpact = false;

	Vec3 rayDisplacement = end - start;

	result.m_impactWorldCoords = start;

	const ChunkCoords chunkCoords = Chunk::GetChunkCoordsAtWorldCoords(start);
	Chunks::const_iterator chunkIter = m_activeChunks.find(chunkCoords);
	if (chunkIter == m_activeChunks.end())
		return result;
	Chunk* chunk = chunkIter->second;
	BlockIndex index = chunk->GetIndexAtWorldCoords(start);

	if (g_blockDefinitions[chunk->m_blocks[index].m_type].m_isVisible) //camera is inside a visible block
		return result;

	float raycastIncrementX = RAYCAST_INCREMENT * rayDisplacement.x;
	float raycastIncrementY = RAYCAST_INCREMENT * rayDisplacement.y;
	float raycastIncrementZ = RAYCAST_INCREMENT * rayDisplacement.z;

	BlockIndex previousIndex = index;

	for (float t = 0.0f; t < 1.0f;){
		while (index == previousIndex){
			t += RAYCAST_INCREMENT;
			if (t >= 1.0f)
				return result;
			result.m_impactWorldCoords += WorldCoords(raycastIncrementX, raycastIncrementY, raycastIncrementZ);

			const ChunkCoords chunkCoords = Chunk::GetChunkCoordsAtWorldCoords(result.m_impactWorldCoords);
			Chunks::const_iterator chunkIter = m_activeChunks.find(chunkCoords);
			if (chunkIter == m_activeChunks.end())
				return result;
			chunk = chunkIter->second;
			index = chunk->GetIndexAtWorldCoords(result.m_impactWorldCoords);
			if (index >= BLOCKS_PER_CHUNK)
				return result;
		}

		if (g_blockDefinitions[chunk->m_blocks[index].m_type].m_isVisible){
			result.m_didImpact = true;

			unsigned short indexMinusPrevIndex = index - previousIndex;
			unsigned short prevIndexMinusIndex = previousIndex - index;

			if ((indexMinusPrevIndex & BLOCKINDEX_Z_MASK) == (1 << (CHUNKS_WIDE_EXPONENT + CHUNKS_LONG_EXPONENT))){ //indexMinusPrevIndex z == 1
				result.m_impactSurfaceNormal = Vec3(0.0f, 0.0f, -1.0f);
				result.m_impactWorldCoordsMins = Vec3(floor(result.m_impactWorldCoords.x), floor(result.m_impactWorldCoords.y), floor(result.m_impactWorldCoords.z));
				result.m_impactWorldCoords.x = result.m_impactWorldCoordsMins.x + 1;
				result.m_impactWorldCoords.y = result.m_impactWorldCoordsMins.y + 1;
				result.m_impactWorldCoords.z = result.m_impactWorldCoordsMins.z;

				result.m_impactFaceCoords.push_back(result.m_impactWorldCoords);
				result.m_impactFaceCoords.push_back(result.m_impactWorldCoords + Vec3(0.0f, -1.0f, 0.0f));
				result.m_impactFaceCoords.push_back(result.m_impactWorldCoords + Vec3(-1.0f, -1.0f, 0.0f));
				result.m_impactFaceCoords.push_back(result.m_impactWorldCoords + Vec3(-1.0f, 0.0f, 0.0f));
			}
			else if ((prevIndexMinusIndex & BLOCKINDEX_Z_MASK) == (1 << (CHUNKS_WIDE_EXPONENT + CHUNKS_LONG_EXPONENT))){ //prevIndexMinusIndex z == 1
				result.m_impactSurfaceNormal = Vec3(0.0f, 0.0f, 1.0f);
				result.m_impactWorldCoordsMins = Vec3(floor(result.m_impactWorldCoords.x), floor(result.m_impactWorldCoords.y), floor(result.m_impactWorldCoords.z));
				result.m_impactWorldCoords.x = result.m_impactWorldCoordsMins.x;
				result.m_impactWorldCoords.y = result.m_impactWorldCoordsMins.y + 1;
				result.m_impactWorldCoords.z = result.m_impactWorldCoordsMins.z + 1;

				result.m_impactFaceCoords.push_back(result.m_impactWorldCoords);
				result.m_impactFaceCoords.push_back(result.m_impactWorldCoords + Vec3(0.0f, -1.0f, 0.0f));
				result.m_impactFaceCoords.push_back(result.m_impactWorldCoords + Vec3(1.0f, -1.0f, 0.0f));
				result.m_impactFaceCoords.push_back(result.m_impactWorldCoords + Vec3(1.0f, 0.0f, 0.0f));
			}
			else if ((indexMinusPrevIndex & BLOCKINDEX_X_MASK) == 1){ //indexMinusPrevIndex x == 1
				result.m_impactSurfaceNormal = Vec3(-1.0f, 0.0f, 0.0f);
				result.m_impactWorldCoordsMins = Vec3(floor(result.m_impactWorldCoords.x), floor(result.m_impactWorldCoords.y), floor(result.m_impactWorldCoords.z));
				result.m_impactWorldCoords.x = result.m_impactWorldCoordsMins.x;
				result.m_impactWorldCoords.y = result.m_impactWorldCoordsMins.y + 1;
				result.m_impactWorldCoords.z = result.m_impactWorldCoordsMins.z;
				
				result.m_impactFaceCoords.push_back(result.m_impactWorldCoords);
				result.m_impactFaceCoords.push_back(result.m_impactWorldCoords + Vec3(0.0f, -1.0f, 0.0f));
				result.m_impactFaceCoords.push_back(result.m_impactWorldCoords + Vec3(0.0f, -1.0f, 1.0f));
				result.m_impactFaceCoords.push_back(result.m_impactWorldCoords + Vec3(0.0f, 0.0f, 1.0f));
			}
			else if ((prevIndexMinusIndex & BLOCKINDEX_X_MASK) == 1){ //prevIndexMinusIndex x == 1
				result.m_impactSurfaceNormal = Vec3(1.0f, 0.0f, 0.0f);
				result.m_impactWorldCoordsMins = Vec3(floor(result.m_impactWorldCoords.x), floor(result.m_impactWorldCoords.y), floor(result.m_impactWorldCoords.z));
				result.m_impactWorldCoords.x = result.m_impactWorldCoordsMins.x + 1;
				result.m_impactWorldCoords.y = result.m_impactWorldCoordsMins.y;
				result.m_impactWorldCoords.z = result.m_impactWorldCoordsMins.z;

				result.m_impactFaceCoords.push_back(result.m_impactWorldCoords);
				result.m_impactFaceCoords.push_back(result.m_impactWorldCoords + Vec3(0.0f, 1.0f, 0.0f));
				result.m_impactFaceCoords.push_back(result.m_impactWorldCoords + Vec3(0.0f, 1.0f, 1.0f));
				result.m_impactFaceCoords.push_back(result.m_impactWorldCoords + Vec3(0.0f, 0.0f, 1.0f));
			}
			else if ((indexMinusPrevIndex & BLOCKINDEX_Y_MASK) == (1 << CHUNKS_WIDE_EXPONENT)){ //indexMinusPrevIndex y == 1
				result.m_impactSurfaceNormal = Vec3(0.0f, -1.0f, 0.0f);
				result.m_impactWorldCoordsMins = Vec3(floor(result.m_impactWorldCoords.x), floor(result.m_impactWorldCoords.y), floor(result.m_impactWorldCoords.z));
				result.m_impactWorldCoords.x = result.m_impactWorldCoordsMins.x;
				result.m_impactWorldCoords.y = result.m_impactWorldCoordsMins.y;
				result.m_impactWorldCoords.z = result.m_impactWorldCoordsMins.z;

				result.m_impactFaceCoords.push_back(result.m_impactWorldCoords);
				result.m_impactFaceCoords.push_back(result.m_impactWorldCoords + Vec3(1.0f, 0.0f, 0.0f));
				result.m_impactFaceCoords.push_back(result.m_impactWorldCoords + Vec3(1.0f, 0.0f, 1.0f));
				result.m_impactFaceCoords.push_back(result.m_impactWorldCoords + Vec3(0.0f, 0.0f, 1.0f));
			}
			else/* if ((prevIndexMinusIndex & BLOCKINDEX_Y_MASK) == (1 << CHUNKS_WIDE_EXPONENT))*/{ //prevIndexMinusIndex y == 1
				result.m_impactSurfaceNormal = Vec3(0.0f, 1.0f, 0.0f);
				result.m_impactWorldCoordsMins = Vec3(floor(result.m_impactWorldCoords.x), floor(result.m_impactWorldCoords.y), floor(result.m_impactWorldCoords.z));
				result.m_impactWorldCoords.x = result.m_impactWorldCoordsMins.x + 1;
				result.m_impactWorldCoords.y = result.m_impactWorldCoordsMins.y + 1;
				result.m_impactWorldCoords.z = result.m_impactWorldCoordsMins.z;

				result.m_impactFaceCoords.push_back(result.m_impactWorldCoords);
				result.m_impactFaceCoords.push_back(result.m_impactWorldCoords + Vec3(-1.0f, 0.0f, 0.0f));
				result.m_impactFaceCoords.push_back(result.m_impactWorldCoords + Vec3(-1.0f, 0.0f, 1.0f));
				result.m_impactFaceCoords.push_back(result.m_impactWorldCoords + Vec3(0.0f, 0.0f, 1.0f));
			}

			return result;
		}
		previousIndex = index;
	}
	return result;
}

///=====================================================
/// 
///=====================================================
void World::RenderSkybox(const OpenGLRenderer* renderer) const{
	static Vec3s bottomVertices;
	static Vec3s topVertices;
	static Vec3s northVertices;
	static Vec3s southVertices;
	static Vec3s eastVertices;
	static Vec3s westVertices;
	if (bottomVertices.empty()){
		bottomVertices.push_back(Vec3(-1.0f, -1.0f, -1.0f));
		bottomVertices.push_back(Vec3(1.0f, -1.0f, -1.0f));
		bottomVertices.push_back(Vec3(1.0f, 1.0f, -1.0f));
		bottomVertices.push_back(Vec3(-1.0f, 1.0f, -1.0f));

		topVertices.push_back(Vec3(1.0f, 1.0f, 1.0f));
		topVertices.push_back(Vec3(1.0f, -1.0f, 1.0f));
		topVertices.push_back(Vec3(-1.0f, -1.0f, 1.0f));
		topVertices.push_back(Vec3(-1.0f, 1.0f, 1.0f));
		
		northVertices.push_back(Vec3(-1.0f, 1.0f, -1.0f));
		northVertices.push_back(Vec3(1.0f, 1.0f, -1.0f));
		northVertices.push_back(Vec3(1.0f, 1.0f, 1.0f));
		northVertices.push_back(Vec3(-1.0f, 1.0f, 1.0f));
		
		southVertices.push_back(Vec3(1.0f, -1.0f, -1.0f));
		southVertices.push_back(Vec3(-1.0f, -1.0f, -1.0f));
		southVertices.push_back(Vec3(-1.0f, -1.0f, 1.0f));
		southVertices.push_back(Vec3(1.0f, -1.0f, 1.0f));
		
		eastVertices.push_back(Vec3(1.0f, 1.0f, -1.0f));
		eastVertices.push_back(Vec3(1.0f, -1.0f, -1.0f));
		eastVertices.push_back(Vec3(1.0f, -1.0f, 1.0f));
		eastVertices.push_back(Vec3(1.0f, 1.0f, 1.0f));
		
		westVertices.push_back(Vec3(-1.0f, -1.0f, -1.0f));
		westVertices.push_back(Vec3(-1.0f, 1.0f, -1.0f));
		westVertices.push_back(Vec3(-1.0f, 1.0f, 1.0f));
		westVertices.push_back(Vec3(-1.0f, -1.0f, 1.0f));
	}

	renderer->SetDepthTest(false);
	renderer->PushMatrix();
	renderer->SetModelViewTranslation(m_camera->m_position);
	renderer->DrawTexturedQuad(*m_skybox, topVertices, m_skybox->CalcTextureCoordinatesAtSpriteNumber(1));
	renderer->DrawTexturedQuad(*m_skybox, bottomVertices, m_skybox->CalcTextureCoordinatesAtSpriteNumber(9));
	renderer->DrawTexturedQuad(*m_skybox, eastVertices, m_skybox->CalcTextureCoordinatesAtSpriteNumber(5));
	renderer->DrawTexturedQuad(*m_skybox, westVertices, m_skybox->CalcTextureCoordinatesAtSpriteNumber(7));
	renderer->DrawTexturedQuad(*m_skybox, northVertices, m_skybox->CalcTextureCoordinatesAtSpriteNumber(4));
	renderer->DrawTexturedQuad(*m_skybox, southVertices, m_skybox->CalcTextureCoordinatesAtSpriteNumber(6));
	renderer->PopMatrix();
	renderer->SetDepthTest(true);
}


///=====================================================
/// 
///=====================================================
void World::PlaceBlockWithRaycast(BlockType blocktype, const Raycast3DResult& raycastResult, BlockLocations& dirtyBlocksList){
	const WorldCoords newBlockCoords(raycastResult.m_impactWorldCoordsMins + raycastResult.m_impactSurfaceNormal);
	BlockIndex index = Chunk::GetIndexAtWorldCoords(newBlockCoords);
	ChunkCoords chunkCoords = Chunk::GetChunkCoordsAtWorldCoords(newBlockCoords);
	Chunks::iterator chunkIter = m_activeChunks.find(chunkCoords);
	if (chunkIter == m_activeChunks.end())
		return;

	Chunk* chunk = chunkIter->second;
	Block& blockToChange = chunk->m_blocks[index];
	if (blockToChange.m_type != BT_AIR){
		return;
	}

	const Vec3s playerBoxContactPoints = GetPlayerBoxContactPoints();
	for (Vec3s::const_iterator cornerIter = playerBoxContactPoints.begin(); cornerIter != playerBoxContactPoints.end(); ++cornerIter){
		if (newBlockCoords == Vec3(floor(cornerIter->x), floor(cornerIter->y), floor(cornerIter->z))) //tried to place a block inside the player's box
			return;
	}

	//Passing this confirms that a new block is being placed

	bool wasSky = blockToChange.IsSky();

	blockToChange.m_type = (unsigned char)blocktype;
	chunk->m_isVboDirty = true;
	if (wasSky)
		blockToChange.UnmarkAsSky();

	const SoundIDs& placeSounds = blockToChange.GetPlaceSounds();
	s_theSoundSystem->PlayRandomSound(placeSounds, 0, 0.25f);

	//update block's and nearby blocks' lighting
	blockToChange.SetLightValue(g_blockDefinitions[blockToChange.m_type].m_inherentLightValue);
	BlockLocation blockLocation(chunk, index);
	if (!blockToChange.IsLightingDirty()){
		if (g_debugPointsEnabled)
			g_debugPositions.push_back(chunk->GetWorldCoordsAtIndex(index));

		dirtyBlocksList.push_back(blockLocation);
		blockToChange.DirtyLighting();
	}
	DirtyNonopaqueNeighbors(blockLocation, true);

	//update sky below placed block
	if (wasSky){
		index -= BLOCKS_PER_CHUNK_LAYER;

		while (index < BLOCKS_PER_CHUNK) {
			Block& block = chunk->m_blocks[index];
			if (block.m_type != BT_AIR){
				return;
			}

			block.UnmarkAsSky();

			if (!block.IsLightingDirty()){
				if (g_debugPointsEnabled)
					g_debugPositions.push_back(chunk->GetWorldCoordsAtIndex(index));

				BlockLocation blockLocation(chunk, index);
				dirtyBlocksList.push_back(blockLocation);
				block.DirtyLighting();
			}
			index -= BLOCKS_PER_CHUNK_LAYER;
		}
	}
}

///=====================================================
/// 
///=====================================================
void World::DestroyBlockWithRaycast(const Raycast3DResult& raycastResult, BlockLocations& dirtyBlocksList){
	BlockIndex index = Chunk::GetIndexAtWorldCoords(raycastResult.m_impactWorldCoordsMins);
	ChunkCoords chunkCoords = Chunk::GetChunkCoordsAtWorldCoords(raycastResult.m_impactWorldCoordsMins);
	Chunks::iterator chunkIter = m_activeChunks.find(chunkCoords);
	if (chunkIter == m_activeChunks.end())
		return;

	Chunk* chunk = chunkIter->second;
	Block& block = chunk->m_blocks[index];

	const SoundIDs& breakSounds = block.GetBreakSounds();
	s_theSoundSystem->PlayRandomSound(breakSounds, 0, 0.25f);

	block.m_type = BT_AIR;

	chunk->m_isVboDirty = true;
	if (!block.IsLightingDirty()){
		if (g_debugPointsEnabled)
			g_debugPositions.push_back(chunk->GetWorldCoordsAtIndex(index));

		BlockLocation blockLocation(chunk, index);
		dirtyBlocksList.push_back(blockLocation);
		block.DirtyLighting();

		//if we are on the edge of a chunk, dirty the adjacent chunk's VBO
		BlockLocation locationToEast = GetBlockLocation(blockLocation, STEP_EAST);
		if (locationToEast.m_chunk != chunk){
			locationToEast.m_chunk->m_isVboDirty = true;
		}
		else{
			BlockLocation locationToWest = GetBlockLocation(blockLocation, STEP_WEST);
			if (locationToWest.m_chunk != chunk){
				locationToWest.m_chunk->m_isVboDirty = true;
			}
		}
		BlockLocation locationToNorth = GetBlockLocation(blockLocation, STEP_NORTH);
		if (locationToNorth.m_chunk != chunk){
			locationToNorth.m_chunk->m_isVboDirty = true;
		}
		else{
			BlockLocation locationToSouth = GetBlockLocation(blockLocation, STEP_SOUTH);
			if (locationToSouth.m_chunk != chunk){
				locationToSouth.m_chunk->m_isVboDirty = true;
			}
		}
	}

	index += BLOCKS_PER_CHUNK_LAYER;
	if (index < BLOCKS_PER_CHUNK){
		Block& blockAbove = chunk->m_blocks[index];
		if (blockAbove.IsSky()){
			block.MarkAsSky();

			index -= BLOCKS_PER_CHUNK_LAYER * 2;

			while (index < BLOCKS_PER_CHUNK) {
				Block& blockBelow = chunk->m_blocks[index];
				if (blockBelow.m_type != BT_AIR){
					return;
				}

				blockBelow.MarkAsSky();

				if (!blockBelow.IsLightingDirty()){
					if (g_debugPointsEnabled)
						g_debugPositions.push_back(chunk->GetWorldCoordsAtIndex(index));

					BlockLocation blockLocation(chunk, index);
					dirtyBlocksList.push_back(blockLocation);
					blockBelow.DirtyLighting();
				}

				index -= BLOCKS_PER_CHUNK_LAYER;
			}
		}
	}
}

///=====================================================
/// 
///=====================================================
const BlockLocation World::GetBlockLocation(const BlockLocation& blockLocation, short indexOffset) const{
	BlockLocation newBlockLocation(blockLocation);
	if (indexOffset == STEP_EAST){ //block to east
		if ((newBlockLocation.m_index & BLOCKINDEX_X_MASK) == BLOCKINDEX_X_MASK){
			Chunk* chunkToEast = newBlockLocation.m_chunk->m_chunkToEast;
			if (chunkToEast){
				newBlockLocation.m_index -= BLOCKINDEX_X_MASK;
				newBlockLocation.m_chunk = chunkToEast;
			}
			else
				newBlockLocation.m_chunk = NULL;
		}
		else{
			newBlockLocation.m_index += indexOffset;
		}
	}
	else if (indexOffset == STEP_WEST){ //block to west
		if ((newBlockLocation.m_index & BLOCKINDEX_X_MASK) == 0){
			Chunk* chunkToWest = newBlockLocation.m_chunk->m_chunkToWest;
			if (chunkToWest){
				newBlockLocation.m_index += BLOCKINDEX_X_MASK;
				newBlockLocation.m_chunk = chunkToWest;
			}
			else
				newBlockLocation.m_chunk = NULL;
		}
		else{
			newBlockLocation.m_index += indexOffset;
		}
	}
	else if (indexOffset == STEP_NORTH){ //block to north
		if ((newBlockLocation.m_index & BLOCKINDEX_Y_MASK) == BLOCKINDEX_Y_MASK){
			Chunk* chunkToNorth = newBlockLocation.m_chunk->m_chunkToNorth;
			if (chunkToNorth){
				newBlockLocation.m_index -= BLOCKINDEX_Y_MASK;
				newBlockLocation.m_chunk = chunkToNorth;
			}
			else
				newBlockLocation.m_chunk = NULL;
		}
		else{
			newBlockLocation.m_index += indexOffset;
		}
	}
	else if (indexOffset == STEP_SOUTH){ //block to south
		if ((newBlockLocation.m_index & BLOCKINDEX_Y_MASK) == 0){
			Chunk* chunkToSouth = newBlockLocation.m_chunk->m_chunkToSouth;
			if (chunkToSouth != NULL){
				newBlockLocation.m_index += BLOCKINDEX_Y_MASK;
				newBlockLocation.m_chunk = chunkToSouth;
			}
			else
				newBlockLocation.m_chunk = NULL;
		}
		else{
			newBlockLocation.m_index += indexOffset;
		}
	}
	else if (indexOffset == STEP_UP || indexOffset == STEP_DOWN){ //block above or below
		BlockIndex newIndex = (BlockIndex)(newBlockLocation.m_index + indexOffset);
		if (newIndex < BLOCKS_PER_CHUNK)
			newBlockLocation.m_index = newIndex;
		else
			newBlockLocation.m_chunk = NULL;
	}

	return newBlockLocation;
}

///=====================================================
/// 
///=====================================================
const Vec3s World::GetPlayerBoxContactPoints() const{
	Vec3s playerBoxContactPoints = m_playerBox.GetCorners();
	float playerWaistHeight = (m_playerBox.maxs.z + m_playerBox.mins.z) * 0.5f;
	playerBoxContactPoints.push_back(Vec3(m_playerBox.maxs.x, m_playerBox.maxs.y, playerWaistHeight));
	playerBoxContactPoints.push_back(Vec3(m_playerBox.mins.x, m_playerBox.maxs.y, playerWaistHeight));
	playerBoxContactPoints.push_back(Vec3(m_playerBox.maxs.x, m_playerBox.mins.y, playerWaistHeight));
	playerBoxContactPoints.push_back(Vec3(m_playerBox.mins.x, m_playerBox.mins.y, playerWaistHeight));
	return playerBoxContactPoints;
}

///=====================================================
/// Returns False if player is not inside the world
///=====================================================
#pragma warning(disable:4127)
bool World::MovePlayerWhenStuckInsideBlocks(){
	while (true){ //while the player is stuck inside blocks, move him up
		const Vec3s playerBoxContactPoints = GetPlayerBoxContactPoints();

		for (Vec3s::const_iterator cornerIter = playerBoxContactPoints.begin(); cornerIter != playerBoxContactPoints.end(); ++cornerIter){
			const ChunkCoords chunkCoords = Chunk::GetChunkCoordsAtWorldCoords(*cornerIter);
			Chunks::const_iterator chunkIter = m_activeChunks.find(chunkCoords);
			if (chunkIter == m_activeChunks.end())
				return false;
			Chunk* chunk = chunkIter->second;
			BlockIndex index = chunk->GetIndexAtWorldCoords(*cornerIter);

			if (g_blockDefinitions[chunk->m_blocks[index].m_type].m_isSolid){ //a corner began inside a solid block
				const static Vec3 MOVE_UP(0.0f, 0.0f, 0.1f);
				m_playerBox.Translate(MOVE_UP);
				m_camera->m_position += MOVE_UP;
			}
			else{
				return true;
			}
		}
	}
}

///=====================================================
/// 
///=====================================================
void World::UpdateSoundAndMusic(double deltaSeconds){
	if (!m_currentMusic || !m_currentMusic->IsPlaying())
		m_currentMusic = s_theSoundSystem->PlayRandomSound(m_music);

	bool isRainingAtPlayer = Chunk::IsRainingAtWorldCoords(m_camera->m_position);
	if (isRainingAtPlayer && (!m_currentRainSound || !m_currentRainSound->IsPlaying())){
		m_currentRainSound = s_theSoundSystem->PlaySound(m_rainSound, -1);
	}
	else if(!isRainingAtPlayer && m_currentRainSound && m_currentRainSound->IsPlaying()){
		m_currentRainSound->Reset();
	}

	if (isRainingAtPlayer){
		if (!m_currentThunderSound || !m_currentThunderSound->IsPlaying()){
			m_timeUntilThunder -= deltaSeconds;
			if (m_timeUntilThunder <= 0.0){
				m_currentThunderSound = s_theSoundSystem->PlayRandomSound(m_thunderSounds);
				m_timeUntilThunder = GetRandomDoubleInRange(2.0, 5.0);
			}
		}
	}
	else if (m_timeUntilThunder < 1.0) //prevent infinite lightning
		m_timeUntilThunder = 0.0;

	if (m_countUntilNextWalkSound <= 0.0 && !m_playerIsInWater){
		Vec3s playerBoxBase;
		playerBoxBase.push_back(Vec3(m_playerBox.mins.x, m_playerBox.mins.y, m_playerBox.mins.z - 0.01f));
		playerBoxBase.push_back(Vec3(m_playerBox.mins.x, m_playerBox.maxs.y, m_playerBox.mins.z - 0.01f));
		playerBoxBase.push_back(Vec3(m_playerBox.maxs.x, m_playerBox.mins.y, m_playerBox.mins.z - 0.01f));
		playerBoxBase.push_back(Vec3(m_playerBox.maxs.x, m_playerBox.maxs.y, m_playerBox.mins.z - 0.01f));

		SoundIDs currentWalkSounds;

		for (Vec3s::const_iterator pointIter = playerBoxBase.begin(); pointIter != playerBoxBase.end(); ++pointIter){
			const ChunkCoords chunkCoords = Chunk::GetChunkCoordsAtWorldCoords(*pointIter);
			Chunks::const_iterator chunkIter = m_activeChunks.find(chunkCoords);
			if (chunkIter == m_activeChunks.end())
				break;
			Chunk* chunk = chunkIter->second;
			BlockIndex index = chunk->GetIndexAtWorldCoords(*pointIter);
			const Block& block = chunk->m_blocks[index];

			if (g_blockDefinitions[block.m_type].m_isSolid){
				const SoundIDs& walkSounds = block.GetWalkSounds();
				assert(!walkSounds.empty());
				currentWalkSounds.push_back(walkSounds.at(GetRandomIntInRange(0, walkSounds.size() - 1)));
			}
		}

		if (!currentWalkSounds.empty()){
			s_theSoundSystem->PlayRandomSound(currentWalkSounds, 0, 0.15f);
			m_countUntilNextWalkSound = GetRandomDoubleInRange(2.0, 2.4);
		}
	}
}
