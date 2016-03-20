//=====================================================
// Chunk.cpp
// by Andrew Socha
//=====================================================

#include "Chunk.hpp"
#include "Engine/Math/Noise.hpp"
#include "BlockDefinition.hpp"
#include "Engine/Core/Utilities.hpp"
#include <sstream>
#include <algorithm>
#include "Engine/Time/Time.hpp"
#include "Engine/Renderer/AnimatedTexture.hpp"

const float TEX_COORD_SIZE_PER_TILE = 1.0f / 32.0f;

unsigned char Chunk::s_tempRLEBuffer[MAX_RLE_BYTES];
WorldCoords Chunk::s_lastKnownCameraPosition;
const float Chunk::AVERAGE_GROUND_HEIGHT = 83.0f;
const float Chunk::SEA_LEVEL = 80.0f;
Vertex3D_PCT_Faces Chunk::s_weatherVertexFaceArray;

const float PERLIN_MINIMUM_PRECIPITATION = 0.6f;
const float PERLIN_MINIMUM_SNOW_BIOME = 0.5f;

///=====================================================
/// 
///=====================================================
void Chunk::RenderWithGLBegin(const OpenGLRenderer* renderer, const AnimatedTexture& textureAtlas) const{
	renderer->PushMatrix();
	renderer->SetModelViewTranslation(m_worldCoordsMins.x, m_worldCoordsMins.y, m_worldCoordsMins.z);
	renderer->BindTexture2D(textureAtlas);
	renderer->BeginQuads();
	for (BlockIndex blockIndex = 0; blockIndex < BLOCKS_PER_CHUNK; ++blockIndex){
		DrawBlockAtIndex(renderer, blockIndex);
	}
	renderer->End();
	renderer->PopMatrix();
}

///=====================================================
/// 
///=====================================================
void Chunk::RenderWithVAs(const OpenGLRenderer* renderer, const AnimatedTexture& texture, bool useWeather, bool isSnow, const Vec2& camForwardNormal, const Vec3& playerPosition) {
	if (useWeather){
		s_weatherVertexFaceArray.clear();
		PopulateVertexFaceArray(s_weatherVertexFaceArray, false, true, isSnow, camForwardNormal, playerPosition);
		if (s_weatherVertexFaceArray.empty()) return;
	}
	else{ //nonopaque blocks
		if (m_translucentBlocksVertexFaceArray.empty()) return;
		std::sort(m_translucentBlocksVertexFaceArray.begin(), m_translucentBlocksVertexFaceArray.end(), SortBlocksFurthestToNearest);
	}

	renderer->PushMatrix();
	renderer->BindTexture2D(texture);
	if (useWeather){
		renderer->WrapTextures();
		renderer->DrawVertexFaceArrayPCT(s_weatherVertexFaceArray);
	}
	else{
		renderer->DrawVertexFaceArrayPCT(m_translucentBlocksVertexFaceArray);
	}
	renderer->PopMatrix();
}

///=====================================================
/// 
///=====================================================
void Chunk::RenderWithVBOs(const OpenGLRenderer* renderer, const AnimatedTexture& textureAtlas){
	if (m_isVboDirty)
		GenerateVertexArrayAndVBO(renderer);

	renderer->PushMatrix();
	renderer->BindTexture2D(textureAtlas);
	renderer->DrawVboPCT(m_vboID, m_numVertexesInVBO);
	renderer->PopMatrix();
}

///=====================================================
/// 
///=====================================================
void Chunk::PopulateVertexFaceArray(Vertex3D_PCT_Faces& out_vertexFaceArray, bool useOpaqueBlocks, bool useWeather, bool isSnow, const Vec2& camForwardNormal, const Vec3& playerPosition) const{
	if (!useWeather){
		if (useOpaqueBlocks)
			out_vertexFaceArray.reserve(400);

		for(BlockIndex blockIndex = 0; blockIndex < BLOCKS_PER_CHUNK; ++blockIndex){
			const Block& block = m_blocks[blockIndex];
			AddBlockVertexesToRenderingArray(block, blockIndex, out_vertexFaceArray, useOpaqueBlocks);
		}
	}
	else{
		int unused;
		const Vec2 player2dCoords(playerPosition);
		for (int column = 0; column < BLOCKS_PER_CHUNK_LAYER; ++column){
			const WorldCoords worldCoords = GetWorldCoordsAtIndex((BlockIndex)column);
			float distanceToPlayerSquared = CalcDistanceSquared(player2dCoords, Vec2(worldCoords));
			if ((distanceToPlayerSquared <= 100.0f && distanceToPlayerSquared > 1.0f) || (distanceToPlayerSquared <= 400.0f && CalculateWeatherAtWorldCoords(playerPosition) < PERLIN_MINIMUM_PRECIPITATION)){
				float biomeForColumn = CalculateBiomeAtWorldCoords(worldCoords, unused);
				if (biomeForColumn >= PERLIN_MINIMUM_SNOW_BIOME && isSnow || biomeForColumn < PERLIN_MINIMUM_SNOW_BIOME && !isSnow){
					float weatherForColumn = CalculateWeatherAtWorldCoords(worldCoords);
					if (weatherForColumn >= PERLIN_MINIMUM_PRECIPITATION){
						for (BlockIndex index = (BlockIndex)(BLOCKS_PER_CHUNK - BLOCKS_PER_CHUNK_LAYER + column); index < BLOCKS_PER_CHUNK; index -= BLOCKS_PER_CHUNK_LAYER){
							const Block& block = m_blocks[index];
							if (!block.IsSky()) break;

							AddWeatherVertexesToRenderingArray(block, index, out_vertexFaceArray, camForwardNormal, isSnow);
						}
					}
				}
			}
		}
	}
}

///=====================================================
/// 
///=====================================================
bool Chunk::SortBlocksFurthestToNearest(const Vertex3D_PCT_Face& vertexFace1, const Vertex3D_PCT_Face& vertexFace2){
	const Vec3 face1MinsPlusMaxes = vertexFace1.vertexes[0].m_position + vertexFace1.vertexes[2].m_position;
	const Vec3 face2MinsPlusMaxes = vertexFace2.vertexes[0].m_position + vertexFace2.vertexes[2].m_position;
	const Vec3 center1(face1MinsPlusMaxes * 0.5f);
	const Vec3 center2(face2MinsPlusMaxes * 0.5f);
	float distanceSquared1 = CalcDistanceSquared(center1, s_lastKnownCameraPosition);
	float distanceSquared2 = CalcDistanceSquared(center2, s_lastKnownCameraPosition);
	return (distanceSquared1 > distanceSquared2);
}

///=====================================================
/// 
///=====================================================
void Chunk::GenerateVertexArrayAndVBO(const OpenGLRenderer* renderer){
	Vertex3D_PCT_Faces vertexFaceArray;
	PopulateVertexFaceArray(vertexFaceArray, true, false, false, Vec2(), Vec3());
	if(m_vboID == 0){
		renderer->GenerateBuffer(&m_vboID);
	}

	m_numVertexesInVBO = vertexFaceArray.size() * 4;
	size_t vertexArrayNumBytes = sizeof(Vertex3D_PCT) * m_numVertexesInVBO;
	renderer->SendVertexDataToBuffer(vertexFaceArray, vertexArrayNumBytes, m_vboID);


	m_translucentBlocksVertexFaceArray.clear();
	PopulateVertexFaceArray(m_translucentBlocksVertexFaceArray, false, false, false, Vec2(), Vec3());

	m_isVboDirty = false;
}

///=====================================================
/// 
///=====================================================
void Chunk::DrawBlockAtIndex(const OpenGLRenderer* renderer, BlockIndex blockIndex) const{
	const Block& block = m_blocks[blockIndex];
	const BlockType& type = (BlockType)(block.m_type);
	const BlockDefinition& blockDef = g_blockDefinitions[type];

	if (!blockDef.m_isVisible) return;

	const LocalCoords blockCoordsMins = GetLocalCoordsAtIndex(blockIndex);
	const LocalCoords blockCoordsMaxs = blockCoordsMins + IntVec3(1, 1, 1);

	const static Vec2 TEX_COORD_SIZE_PER_TILE_VEC2(TEX_COORD_SIZE_PER_TILE, TEX_COORD_SIZE_PER_TILE);

	BlockIndex aboveBlockIndex = blockIndex + BLOCKS_PER_CHUNK_LAYER;
	if (aboveBlockIndex < BLOCKS_PER_CHUNK && !g_blockDefinitions[m_blocks[aboveBlockIndex].m_type].m_isOpaque){
		const Vec2& topTexCoordsMins = blockDef.m_topTexCoordsMins;
		const Vec2 topTexCoordsMaxs = topTexCoordsMins + TEX_COORD_SIZE_PER_TILE_VEC2;

		renderer->TexCoord2f(topTexCoordsMins.x, topTexCoordsMaxs.y);
		renderer->Vertex3i(blockCoordsMins.x, blockCoordsMins.y, blockCoordsMaxs.z);
		renderer->TexCoord2f(topTexCoordsMaxs.x, topTexCoordsMaxs.y);
		renderer->Vertex3i(blockCoordsMaxs.x, blockCoordsMins.y, blockCoordsMaxs.z);
		renderer->TexCoord2f(topTexCoordsMaxs.x, topTexCoordsMins.y);
		renderer->Vertex3i(blockCoordsMaxs.x, blockCoordsMaxs.y, blockCoordsMaxs.z);
		renderer->TexCoord2f(topTexCoordsMins.x, topTexCoordsMins.y);
		renderer->Vertex3i(blockCoordsMins.x, blockCoordsMaxs.y, blockCoordsMaxs.z);
	}

	BlockIndex belowBlockIndex = blockIndex - BLOCKS_PER_CHUNK_LAYER;
	if (belowBlockIndex < BLOCKS_PER_CHUNK && !g_blockDefinitions[m_blocks[belowBlockIndex].m_type].m_isOpaque){
		const Vec2& bottomTexCoordsMins = blockDef.m_bottomTexCoordsMins;
		const Vec2 bottomTexCoordsMaxs = bottomTexCoordsMins + TEX_COORD_SIZE_PER_TILE_VEC2;

		renderer->TexCoord2f(bottomTexCoordsMins.x, bottomTexCoordsMaxs.y);
		renderer->Vertex3i(blockCoordsMaxs.x, blockCoordsMins.y, blockCoordsMins.z);
		renderer->TexCoord2f(bottomTexCoordsMaxs.x, bottomTexCoordsMaxs.y);
		renderer->Vertex3i(blockCoordsMins.x, blockCoordsMins.y, blockCoordsMins.z);
		renderer->TexCoord2f(bottomTexCoordsMaxs.x, bottomTexCoordsMins.y);
		renderer->Vertex3i(blockCoordsMins.x, blockCoordsMaxs.y, blockCoordsMins.z);
		renderer->TexCoord2f(bottomTexCoordsMins.x, bottomTexCoordsMins.y);
		renderer->Vertex3i(blockCoordsMaxs.x, blockCoordsMaxs.y, blockCoordsMins.z);
	}

	const Vec2& sideTexCoordsMins = blockDef.m_sideTexCoordsMins;
	const Vec2 sideTexCoordsMaxs = sideTexCoordsMins + TEX_COORD_SIZE_PER_TILE_VEC2;

	if (((blockIndex & CHUNK_LAYER_MASK) >> CHUNKS_WIDE_EXPONENT) != (CHUNK_LAYER_MASK >> CHUNKS_WIDE_EXPONENT)){ //we are not on the northern edge of the chunk
		BlockIndex northBlockIndex = blockIndex + BLOCKS_PER_CHUNK_X;
		if (!g_blockDefinitions[m_blocks[northBlockIndex].m_type].m_isOpaque){
			renderer->TexCoord2f(sideTexCoordsMins.x, sideTexCoordsMaxs.y);
			renderer->Vertex3i(blockCoordsMaxs.x, blockCoordsMaxs.y, blockCoordsMins.z);
			renderer->TexCoord2f(sideTexCoordsMaxs.x, sideTexCoordsMaxs.y);
			renderer->Vertex3i(blockCoordsMins.x, blockCoordsMaxs.y, blockCoordsMins.z);
			renderer->TexCoord2f(sideTexCoordsMaxs.x, sideTexCoordsMins.y);
			renderer->Vertex3i(blockCoordsMins.x, blockCoordsMaxs.y, blockCoordsMaxs.z);
			renderer->TexCoord2f(sideTexCoordsMins.x, sideTexCoordsMins.y);
			renderer->Vertex3i(blockCoordsMaxs.x, blockCoordsMaxs.y, blockCoordsMaxs.z);
		}
	}

	if (((blockIndex & CHUNK_LAYER_MASK) >> CHUNKS_WIDE_EXPONENT) != 0){ //we are not on the southern edge of the chunk
		BlockIndex southBlockIndex = blockIndex - BLOCKS_PER_CHUNK_X;
		if (!g_blockDefinitions[m_blocks[southBlockIndex].m_type].m_isOpaque){
			renderer->TexCoord2f(sideTexCoordsMins.x, sideTexCoordsMaxs.y);
			renderer->Vertex3i(blockCoordsMins.x, blockCoordsMins.y, blockCoordsMins.z);
			renderer->TexCoord2f(sideTexCoordsMaxs.x, sideTexCoordsMaxs.y);
			renderer->Vertex3i(blockCoordsMaxs.x, blockCoordsMins.y, blockCoordsMins.z);
			renderer->TexCoord2f(sideTexCoordsMaxs.x, sideTexCoordsMins.y);
			renderer->Vertex3i(blockCoordsMaxs.x, blockCoordsMins.y, blockCoordsMaxs.z);
			renderer->TexCoord2f(sideTexCoordsMins.x, sideTexCoordsMins.y);
			renderer->Vertex3i(blockCoordsMins.x, blockCoordsMins.y, blockCoordsMaxs.z);
		}
	}

	if ((blockIndex & CHUNK_X_MASK) != CHUNK_X_MASK){ //we are not on the eastern edge of the chunk
		BlockIndex eastBlockIndex = blockIndex + 1;
		if (!g_blockDefinitions[m_blocks[eastBlockIndex].m_type].m_isOpaque){
			renderer->TexCoord2f(sideTexCoordsMins.x, sideTexCoordsMaxs.y);
			renderer->Vertex3i(blockCoordsMaxs.x, blockCoordsMins.y, blockCoordsMins.z);
			renderer->TexCoord2f(sideTexCoordsMaxs.x, sideTexCoordsMaxs.y);
			renderer->Vertex3i(blockCoordsMaxs.x, blockCoordsMaxs.y, blockCoordsMins.z);
			renderer->TexCoord2f(sideTexCoordsMaxs.x, sideTexCoordsMins.y);
			renderer->Vertex3i(blockCoordsMaxs.x, blockCoordsMaxs.y, blockCoordsMaxs.z);
			renderer->TexCoord2f(sideTexCoordsMins.x, sideTexCoordsMins.y);
			renderer->Vertex3i(blockCoordsMaxs.x, blockCoordsMins.y, blockCoordsMaxs.z);
		}
	}

	if ((blockIndex & CHUNK_X_MASK) != 0){ //we are not on the western edge of the chunk
		BlockIndex westBlockIndex = blockIndex - 1;
		if (!g_blockDefinitions[m_blocks[westBlockIndex].m_type].m_isOpaque){
			renderer->TexCoord2f(sideTexCoordsMins.x, sideTexCoordsMaxs.y);
			renderer->Vertex3i(blockCoordsMins.x, blockCoordsMaxs.y, blockCoordsMins.z);
			renderer->TexCoord2f(sideTexCoordsMaxs.x, sideTexCoordsMaxs.y);
			renderer->Vertex3i(blockCoordsMins.x, blockCoordsMins.y, blockCoordsMins.z);
			renderer->TexCoord2f(sideTexCoordsMaxs.x, sideTexCoordsMins.y);
			renderer->Vertex3i(blockCoordsMins.x, blockCoordsMins.y, blockCoordsMaxs.z);
			renderer->TexCoord2f(sideTexCoordsMins.x, sideTexCoordsMins.y);
			renderer->Vertex3i(blockCoordsMins.x, blockCoordsMaxs.y, blockCoordsMaxs.z);
		}
	}
}

///=====================================================
/// 
///=====================================================
void Chunk::Update(double /*deltaSeconds*/){

}

///=====================================================
/// 
///=====================================================
void Chunk::PopulateWithBlocks(){
	//determine ground height
	int groundHeightForEachColumn[BLOCKS_PER_CHUNK_LAYER];
	int dirtHeightForEachColumn[BLOCKS_PER_CHUNK_LAYER];
	float biomeForEachColumn[BLOCKS_PER_CHUNK_LAYER];
	for (int column = 0; column < BLOCKS_PER_CHUNK_LAYER; ++column){
		const WorldCoords worldCoords = GetWorldCoordsAtIndex((BlockIndex)column);
		dirtHeightForEachColumn[column] = (int)(10.0f + ComputePerlinNoiseValueAtPosition2D(Vec2(worldCoords.x, worldCoords.y), 40.0f, 8, 6.0f, 0.5f));
		biomeForEachColumn[column] = CalculateBiomeAtWorldCoords(worldCoords, groundHeightForEachColumn[column]);
	}

	//determine block type
	for (BlockIndex index = 0; index < BLOCKS_PER_CHUNK; ++index){
		Block& block = m_blocks[index];
		const LocalCoords localCoords = GetLocalCoordsAtIndex(index);
		int height = (index & BLOCKINDEX_Z_MASK) >> (CHUNKS_WIDE_EXPONENT + CHUNKS_LONG_EXPONENT);
		int column = index & CHUNK_LAYER_MASK;
		if (height > groundHeightForEachColumn[column]){
			if (height > (int)SEA_LEVEL)
				block.m_type = BT_AIR;
			else{
				if (biomeForEachColumn[column] < PERLIN_MINIMUM_SNOW_BIOME + (SEA_LEVEL - height) * 0.03f)
					block.m_type = BT_WATER;
				else
					block.m_type = BT_ICE;
			}
		}
		else if (height == groundHeightForEachColumn[column]){
			if (height == (int)SEA_LEVEL){
				if (biomeForEachColumn[column] < PERLIN_MINIMUM_SNOW_BIOME)
					block.m_type = BT_SAND;
				else
					block.m_type = BT_SNOW;
			}
			else if (height > (int)SEA_LEVEL){
				if (biomeForEachColumn[column] < PERLIN_MINIMUM_SNOW_BIOME)
					block.m_type = BT_GRASS;
				else
					block.m_type = BT_SNOW;
			}
			else
				block.m_type = BT_DIRT;
		}
		else if (height > (groundHeightForEachColumn[column] - dirtHeightForEachColumn[column]))
			block.m_type = BT_DIRT;
		else
			block.m_type = BT_STONE;

		block.m_lightingAndFlags = g_blockDefinitions[block.m_type].m_inherentLightValue;
	}
}

///=====================================================
/// 
///=====================================================
void Chunk::SaveToDisk() const{
	size_t rleBufferSize;
	unsigned char* rleBuffer = CreateRLEBuffer(rleBufferSize);
	std::string mapFilePath = GetFilePath();

	WriteBufferToFile(rleBuffer, rleBufferSize, mapFilePath);

	delete rleBuffer;
}

///=====================================================
/// 
///=====================================================
unsigned char* Chunk::CreateRLEBuffer(size_t& out_rleBufferSize) const{
	std::vector<unsigned char> buffer;
	buffer.reserve(4096);

	unsigned char currentBlockType = m_blocks[0].m_type;
	unsigned short currentBlockCount = 1;
	for (BlockIndex index = 1; index < BLOCKS_PER_CHUNK; ++index){ //start at 2nd block, previously initialized with first block
		const Block& block = m_blocks[index];
		if (block.m_type != currentBlockType){ //start new RLE sequence
			AppendToRLEBuffer(currentBlockType, currentBlockCount, buffer);

			currentBlockType = block.m_type;
			currentBlockCount = 1;
		}
		else{
			++currentBlockCount;
		}
	}

	//end sequence with final block type
	AppendToRLEBuffer(currentBlockType, currentBlockCount, buffer);

	out_rleBufferSize = buffer.size();

	unsigned char* rleBuffer = new unsigned char[out_rleBufferSize];
	memcpy(rleBuffer, buffer.data(), out_rleBufferSize * sizeof(buffer[0]));

	return rleBuffer;
}

///=====================================================
/// 
///=====================================================
std::string Chunk::GetFilePath() const{
	const ChunkCoords chunkCoords = GetChunkCoordsAtWorldCoords(m_worldCoordsMins);

	std::stringstream ss;
	ss << "Data/Chunks/Chunk" << chunkCoords.x << "," << chunkCoords.y << ".chunk";
	return ss.str();
}

///=====================================================
/// 
///=====================================================
void Chunk::PopulateFromTempRLEBuffer(){
	int bufferIndex = 0;
	unsigned char currentBlockType = s_tempRLEBuffer[bufferIndex];
	unsigned short currentBlockCount = 0;
	unsigned short numBlocksAssigned = 0;
	for (BlockIndex index = 0; index < BLOCKS_PER_CHUNK; ++index){
		if (numBlocksAssigned == currentBlockCount){
			currentBlockType = s_tempRLEBuffer[bufferIndex++];
			currentBlockCount = (s_tempRLEBuffer[bufferIndex++] << 8);
			currentBlockCount += s_tempRLEBuffer[bufferIndex++];
			numBlocksAssigned = 0;
		}

		Block& block = m_blocks[index];
		block.m_type = currentBlockType;
		block.m_lightingAndFlags = g_blockDefinitions[block.m_type].m_inherentLightValue;
		++numBlocksAssigned;
	}
}

///=====================================================
/// 
///=====================================================
void Chunk::AppendToRLEBuffer(unsigned char blockType, unsigned short blockCount, std::vector<unsigned char>& buffer) const{
	buffer.push_back(blockType);
	buffer.push_back((unsigned char)(blockCount >> 8));
	buffer.push_back((unsigned char)blockCount);
}

///=====================================================
/// 
///=====================================================
bool Chunk::LoadFromDisk(){
	std::string mapFilePath = GetFilePath();

	bool loaded = LoadFileToExistingBuffer(mapFilePath, s_tempRLEBuffer, MAX_RLE_BYTES);
	if (loaded){
		PopulateFromTempRLEBuffer();
	}
	return loaded;
}

///=====================================================
/// 
///=====================================================
void Chunk::PlaceBlockBeneathCoords(BlockType blocktype, const WorldCoords& worldCoords, BlockLocations& dirtyBlocksList){
	BlockIndex index = Chunk::GetIndexAtWorldCoords(worldCoords);
	const Block& playersBlock = m_blocks[index];
	if (playersBlock.m_type != BT_AIR){ //inside a solid block, so can't place a block
		return;
	}
	index -= BLOCKS_PER_CHUNK_LAYER;

	while (index < BLOCKS_PER_CHUNK) {
		const Block& block = m_blocks[index];
		if (block.m_type != BT_AIR){
			index += BLOCKS_PER_CHUNK_LAYER;
			Block& blockToChange = m_blocks[index];
			blockToChange.m_type = (unsigned char)blocktype;
			m_isVboDirty = true;
			blockToChange.UnmarkAsSky();

			if (!block.IsLightingDirty()){
				if (g_debugPointsEnabled)
					g_debugPositions.push_back(GetWorldCoordsAtIndex(index));

				BlockLocation blockLocation(this, index);
				dirtyBlocksList.push_back(blockLocation);
				blockToChange.SetLightValue(g_blockDefinitions[blockToChange.m_type].m_inherentLightValue);
				//DirtyNonopaqueNeighbors(blockLocation, true); //can't do this from chunk, so lighting won't update properly
			}

			return;
		}
		index -= BLOCKS_PER_CHUNK_LAYER;
	}
}

///=====================================================
/// 
///=====================================================
void Chunk::DestroyBlockBeneathCoords(const WorldCoords& worldCoords, BlockLocations& dirtyBlocksList){
	BlockIndex index = Chunk::GetIndexAtWorldCoords(worldCoords);
	while (index < BLOCKS_PER_CHUNK) {
		Block& block = m_blocks[index];
		if (block.m_type != BT_AIR){
			block.m_type = BT_AIR;
			m_isVboDirty = true;

			while (index < BLOCKS_PER_CHUNK){ //update new sky below destroyed block
				Block& block = m_blocks[index];
				block.MarkAsSky();
				
				if (!block.IsLightingDirty()){
					if (g_debugPointsEnabled)
						g_debugPositions.push_back(GetWorldCoordsAtIndex(index));

					BlockLocation blockLocation(this, index);
					dirtyBlocksList.push_back(blockLocation);
					block.DirtyLighting();
				}

				index -= BLOCKS_PER_CHUNK_LAYER;
			}

			return;
		}
		index -= BLOCKS_PER_CHUNK_LAYER;
	}
}

///=====================================================
/// 
///=====================================================
void Chunk::AddBlockVertexesToRenderingArray(const Block& block, BlockIndex blockIndex, Vertex3D_PCT_Faces& out_vertexFaceArray, bool useOpaqueBlocks) const{
	const BlockType& type = (BlockType)(block.m_type);
	const BlockDefinition& blockDef = g_blockDefinitions[type];

	if (!blockDef.m_isVisible) return;
	if (blockDef.m_isOpaque != useOpaqueBlocks) return;

	const WorldCoords blockCoordsMins = GetWorldCoordsAtIndex(blockIndex);
	const WorldCoords blockCoordsMaxs = blockCoordsMins + Vec3(1.0f, 1.0f, 1.0f);

	const static Vec2 TEX_COORD_SIZE_PER_TILE_VEC2(TEX_COORD_SIZE_PER_TILE, TEX_COORD_SIZE_PER_TILE);

	Vertex3D_PCT vertex;
	Vertex3D_PCT_Face vertexFace;

	BlockIndex aboveBlockIndex = blockIndex + BLOCKS_PER_CHUNK_LAYER;
	if (aboveBlockIndex < BLOCKS_PER_CHUNK){
		const Block& aboveBlock = m_blocks[aboveBlockIndex];
		if (!g_blockDefinitions[aboveBlock.m_type].m_isOpaque && aboveBlock.m_type != type){
			const Vec2& topTexCoordsMins = blockDef.m_topTexCoordsMins;
			const Vec2 topTexCoordsMaxs = topTexCoordsMins + TEX_COORD_SIZE_PER_TILE_VEC2;
			unsigned char lighting = aboveBlock.GetLightValue() << 4;
			vertex.m_color = RGBAchars(lighting, lighting, lighting);

			vertex.m_texCoords = Vec2(topTexCoordsMins.x, topTexCoordsMaxs.y);
			vertex.m_position = Vec3(blockCoordsMins.x, blockCoordsMins.y, blockCoordsMaxs.z);
			vertexFace.vertexes[0] = vertex;
			vertex.m_texCoords = Vec2(topTexCoordsMaxs.x, topTexCoordsMaxs.y);
			vertex.m_position = Vec3(blockCoordsMaxs.x, blockCoordsMins.y, blockCoordsMaxs.z);
			vertexFace.vertexes[1] = vertex;
			vertex.m_texCoords = Vec2(topTexCoordsMaxs.x, topTexCoordsMins.y);
			vertex.m_position = Vec3(blockCoordsMaxs.x, blockCoordsMaxs.y, blockCoordsMaxs.z);
			vertexFace.vertexes[2] = vertex;
			vertex.m_texCoords = Vec2(topTexCoordsMins.x, topTexCoordsMins.y);
			vertex.m_position = Vec3(blockCoordsMins.x, blockCoordsMaxs.y, blockCoordsMaxs.z);
			vertexFace.vertexes[3] = vertex;
			out_vertexFaceArray.push_back(vertexFace);

			if (type == BT_WATER && aboveBlock.m_type == BT_AIR){ //create inner face for water, so you can see the top of water while inside it
				vertex.m_position = vertexFace.vertexes[0].m_position;
				vertexFace.vertexes[0].m_position = vertexFace.vertexes[1].m_position;
				vertexFace.vertexes[1].m_position = vertex.m_position;

				vertex.m_position = vertexFace.vertexes[2].m_position;
				vertexFace.vertexes[2].m_position = vertexFace.vertexes[3].m_position;
				vertexFace.vertexes[3].m_position = vertex.m_position;

				out_vertexFaceArray.push_back(vertexFace);
			}
		}
	}

	BlockIndex belowBlockIndex = blockIndex - BLOCKS_PER_CHUNK_LAYER;
	if (belowBlockIndex < BLOCKS_PER_CHUNK){
		const Block& belowBlock = m_blocks[belowBlockIndex];
		if (!g_blockDefinitions[belowBlock.m_type].m_isOpaque && belowBlock.m_type != type){
			const Vec2& bottomTexCoordsMins = blockDef.m_bottomTexCoordsMins;
			const Vec2 bottomTexCoordsMaxs = bottomTexCoordsMins + TEX_COORD_SIZE_PER_TILE_VEC2;
			unsigned char lighting = belowBlock.GetLightValue() << 4;
			vertex.m_color = RGBAchars(lighting, lighting, lighting);

			vertex.m_texCoords = Vec2(bottomTexCoordsMins.x, bottomTexCoordsMaxs.y);
			vertex.m_position = Vec3(blockCoordsMaxs.x, blockCoordsMins.y, blockCoordsMins.z);
			vertexFace.vertexes[0] = vertex;
			vertex.m_texCoords = Vec2(bottomTexCoordsMaxs.x, bottomTexCoordsMaxs.y);
			vertex.m_position = Vec3(blockCoordsMins.x, blockCoordsMins.y, blockCoordsMins.z);
			vertexFace.vertexes[1] = vertex;
			vertex.m_texCoords = Vec2(bottomTexCoordsMaxs.x, bottomTexCoordsMins.y);
			vertex.m_position = Vec3(blockCoordsMins.x, blockCoordsMaxs.y, blockCoordsMins.z);
			vertexFace.vertexes[2] = vertex;
			vertex.m_texCoords = Vec2(bottomTexCoordsMins.x, bottomTexCoordsMins.y);
			vertex.m_position = Vec3(blockCoordsMaxs.x, blockCoordsMaxs.y, blockCoordsMins.z);
			vertexFace.vertexes[3] = vertex;
			out_vertexFaceArray.push_back(vertexFace);
		}
	}

	const Vec2& sideTexCoordsMins = blockDef.m_sideTexCoordsMins;
	const Vec2 sideTexCoordsMaxs = sideTexCoordsMins + TEX_COORD_SIZE_PER_TILE_VEC2;
	if (((blockIndex & CHUNK_LAYER_MASK) >> CHUNKS_WIDE_EXPONENT) != (CHUNK_LAYER_MASK >> CHUNKS_WIDE_EXPONENT)){ //we are not on the northern edge of the chunk
		BlockIndex northBlockIndex = blockIndex + BLOCKS_PER_CHUNK_X;
		const Block& northBlock = m_blocks[northBlockIndex];
		if (!g_blockDefinitions[northBlock.m_type].m_isOpaque && northBlock.m_type != type){
			unsigned char lighting = northBlock.GetLightValue() << 4;
			vertex.m_color = RGBAchars(lighting, lighting, lighting);

			vertex.m_texCoords = Vec2(sideTexCoordsMins.x, sideTexCoordsMaxs.y);
			vertex.m_position = Vec3(blockCoordsMaxs.x, blockCoordsMaxs.y, blockCoordsMins.z);
			vertexFace.vertexes[0] = vertex;
			vertex.m_texCoords = Vec2(sideTexCoordsMaxs.x, sideTexCoordsMaxs.y);
			vertex.m_position = Vec3(blockCoordsMins.x, blockCoordsMaxs.y, blockCoordsMins.z);
			vertexFace.vertexes[1] = vertex;
			vertex.m_texCoords = Vec2(sideTexCoordsMaxs.x, sideTexCoordsMins.y);
			vertex.m_position = Vec3(blockCoordsMins.x, blockCoordsMaxs.y, blockCoordsMaxs.z);
			vertexFace.vertexes[2] = vertex;
			vertex.m_texCoords = Vec2(sideTexCoordsMins.x, sideTexCoordsMins.y);
			vertex.m_position = Vec3(blockCoordsMaxs.x, blockCoordsMaxs.y, blockCoordsMaxs.z);
			vertexFace.vertexes[3] = vertex;
			out_vertexFaceArray.push_back(vertexFace);
		}
	}
	else if (m_chunkToNorth){
		BlockIndex northBlockIndex = blockIndex & ~BLOCKINDEX_Y_MASK;
		const Block& northBlock = m_chunkToNorth->m_blocks[northBlockIndex];
		if (!g_blockDefinitions[northBlock.m_type].m_isOpaque && northBlock.m_type != type){
			unsigned char lighting = northBlock.GetLightValue() << 4;
			vertex.m_color = RGBAchars(lighting, lighting, lighting);

			vertex.m_texCoords = Vec2(sideTexCoordsMins.x, sideTexCoordsMaxs.y);
			vertex.m_position = Vec3(blockCoordsMaxs.x, blockCoordsMaxs.y, blockCoordsMins.z);
			vertexFace.vertexes[0] = vertex;
			vertex.m_texCoords = Vec2(sideTexCoordsMaxs.x, sideTexCoordsMaxs.y);
			vertex.m_position = Vec3(blockCoordsMins.x, blockCoordsMaxs.y, blockCoordsMins.z);
			vertexFace.vertexes[1] = vertex;
			vertex.m_texCoords = Vec2(sideTexCoordsMaxs.x, sideTexCoordsMins.y);
			vertex.m_position = Vec3(blockCoordsMins.x, blockCoordsMaxs.y, blockCoordsMaxs.z);
			vertexFace.vertexes[2] = vertex;
			vertex.m_texCoords = Vec2(sideTexCoordsMins.x, sideTexCoordsMins.y);
			vertex.m_position = Vec3(blockCoordsMaxs.x, blockCoordsMaxs.y, blockCoordsMaxs.z);
			vertexFace.vertexes[3] = vertex;
			out_vertexFaceArray.push_back(vertexFace);
		}
	}

	if (((blockIndex & CHUNK_LAYER_MASK) >> CHUNKS_WIDE_EXPONENT) != 0){ //we are not on the southern edge of the chunk
		BlockIndex southBlockIndex = blockIndex - BLOCKS_PER_CHUNK_X;
		const Block& southBlock = m_blocks[southBlockIndex];
		if (!g_blockDefinitions[southBlock.m_type].m_isOpaque && southBlock.m_type != type){
			unsigned char lighting = southBlock.GetLightValue() << 4;
			vertex.m_color = RGBAchars(lighting, lighting, lighting);

			vertex.m_texCoords = Vec2(sideTexCoordsMins.x, sideTexCoordsMaxs.y);
			vertex.m_position = Vec3(blockCoordsMins.x, blockCoordsMins.y, blockCoordsMins.z);
			vertexFace.vertexes[0] = vertex;
			vertex.m_texCoords = Vec2(sideTexCoordsMaxs.x, sideTexCoordsMaxs.y);
			vertex.m_position = Vec3(blockCoordsMaxs.x, blockCoordsMins.y, blockCoordsMins.z);
			vertexFace.vertexes[1] = vertex;
			vertex.m_texCoords = Vec2(sideTexCoordsMaxs.x, sideTexCoordsMins.y);
			vertex.m_position = Vec3(blockCoordsMaxs.x, blockCoordsMins.y, blockCoordsMaxs.z);
			vertexFace.vertexes[2] = vertex;
			vertex.m_texCoords = Vec2(sideTexCoordsMins.x, sideTexCoordsMins.y);
			vertex.m_position = Vec3(blockCoordsMins.x, blockCoordsMins.y, blockCoordsMaxs.z);
			vertexFace.vertexes[3] = vertex;
			out_vertexFaceArray.push_back(vertexFace);
		}
	}
	else if (m_chunkToSouth){
		BlockIndex southBlockIndex = blockIndex | BLOCKINDEX_Y_MASK;
		const Block& southBlock = m_chunkToSouth->m_blocks[southBlockIndex];
		if (!g_blockDefinitions[southBlock.m_type].m_isOpaque && southBlock.m_type != type){
			unsigned char lighting = southBlock.GetLightValue() << 4;
			vertex.m_color = RGBAchars(lighting, lighting, lighting);

			vertex.m_texCoords = Vec2(sideTexCoordsMins.x, sideTexCoordsMaxs.y);
			vertex.m_position = Vec3(blockCoordsMins.x, blockCoordsMins.y, blockCoordsMins.z);
			vertexFace.vertexes[0] = vertex;
			vertex.m_texCoords = Vec2(sideTexCoordsMaxs.x, sideTexCoordsMaxs.y);
			vertex.m_position = Vec3(blockCoordsMaxs.x, blockCoordsMins.y, blockCoordsMins.z);
			vertexFace.vertexes[1] = vertex;
			vertex.m_texCoords = Vec2(sideTexCoordsMaxs.x, sideTexCoordsMins.y);
			vertex.m_position = Vec3(blockCoordsMaxs.x, blockCoordsMins.y, blockCoordsMaxs.z);
			vertexFace.vertexes[2] = vertex;
			vertex.m_texCoords = Vec2(sideTexCoordsMins.x, sideTexCoordsMins.y);
			vertex.m_position = Vec3(blockCoordsMins.x, blockCoordsMins.y, blockCoordsMaxs.z);
			vertexFace.vertexes[3] = vertex;
			out_vertexFaceArray.push_back(vertexFace);
		}
	}

	if ((blockIndex & CHUNK_X_MASK) != CHUNK_X_MASK){ //we are not on the eastern edge of the chunk
		BlockIndex eastBlockIndex = blockIndex + 1;
		const Block& eastBlock = m_blocks[eastBlockIndex];
		if (!g_blockDefinitions[eastBlock.m_type].m_isOpaque && eastBlock.m_type != type){
			unsigned char lighting = eastBlock.GetLightValue() << 4;
			vertex.m_color = RGBAchars(lighting, lighting, lighting);

			vertex.m_texCoords = Vec2(sideTexCoordsMins.x, sideTexCoordsMaxs.y);
			vertex.m_position = Vec3(blockCoordsMaxs.x, blockCoordsMins.y, blockCoordsMins.z);
			vertexFace.vertexes[0] = vertex;
			vertex.m_texCoords = Vec2(sideTexCoordsMaxs.x, sideTexCoordsMaxs.y);
			vertex.m_position = Vec3(blockCoordsMaxs.x, blockCoordsMaxs.y, blockCoordsMins.z);
			vertexFace.vertexes[1] = vertex;
			vertex.m_texCoords = Vec2(sideTexCoordsMaxs.x, sideTexCoordsMins.y);
			vertex.m_position = Vec3(blockCoordsMaxs.x, blockCoordsMaxs.y, blockCoordsMaxs.z);
			vertexFace.vertexes[2] = vertex;
			vertex.m_texCoords = Vec2(sideTexCoordsMins.x, sideTexCoordsMins.y);
			vertex.m_position = Vec3(blockCoordsMaxs.x, blockCoordsMins.y, blockCoordsMaxs.z);
			vertexFace.vertexes[3] = vertex;
			out_vertexFaceArray.push_back(vertexFace);
		}
	}
	else if (m_chunkToEast){
		BlockIndex eastBlockIndex = blockIndex & ~BLOCKINDEX_X_MASK;
		const Block& eastBlock = m_chunkToEast->m_blocks[eastBlockIndex];
		if (!g_blockDefinitions[eastBlock.m_type].m_isOpaque && eastBlock.m_type != type){
			unsigned char lighting = eastBlock.GetLightValue() << 4;
			vertex.m_color = RGBAchars(lighting, lighting, lighting);

			vertex.m_texCoords = Vec2(sideTexCoordsMins.x, sideTexCoordsMaxs.y);
			vertex.m_position = Vec3(blockCoordsMaxs.x, blockCoordsMins.y, blockCoordsMins.z);
			vertexFace.vertexes[0] = vertex;
			vertex.m_texCoords = Vec2(sideTexCoordsMaxs.x, sideTexCoordsMaxs.y);
			vertex.m_position = Vec3(blockCoordsMaxs.x, blockCoordsMaxs.y, blockCoordsMins.z);
			vertexFace.vertexes[1] = vertex;
			vertex.m_texCoords = Vec2(sideTexCoordsMaxs.x, sideTexCoordsMins.y);
			vertex.m_position = Vec3(blockCoordsMaxs.x, blockCoordsMaxs.y, blockCoordsMaxs.z);
			vertexFace.vertexes[2] = vertex;
			vertex.m_texCoords = Vec2(sideTexCoordsMins.x, sideTexCoordsMins.y);
			vertex.m_position = Vec3(blockCoordsMaxs.x, blockCoordsMins.y, blockCoordsMaxs.z);
			vertexFace.vertexes[3] = vertex;
			out_vertexFaceArray.push_back(vertexFace);
		}
	}

	if ((blockIndex & CHUNK_X_MASK) != 0){ //we are not on the western edge of the chunk
		BlockIndex westBlockIndex = blockIndex - 1;
		const Block& westBlock = m_blocks[westBlockIndex];
		if (!g_blockDefinitions[westBlock.m_type].m_isOpaque && westBlock.m_type != type){
			unsigned char lighting = westBlock.GetLightValue() << 4;
			vertex.m_color = RGBAchars(lighting, lighting, lighting);

			vertex.m_texCoords = Vec2(sideTexCoordsMins.x, sideTexCoordsMaxs.y);
			vertex.m_position = Vec3(blockCoordsMins.x, blockCoordsMaxs.y, blockCoordsMins.z);
			vertexFace.vertexes[0] = vertex;
			vertex.m_texCoords = Vec2(sideTexCoordsMaxs.x, sideTexCoordsMaxs.y);
			vertex.m_position = Vec3(blockCoordsMins.x, blockCoordsMins.y, blockCoordsMins.z);
			vertexFace.vertexes[1] = vertex;
			vertex.m_texCoords = Vec2(sideTexCoordsMaxs.x, sideTexCoordsMins.y);
			vertex.m_position = Vec3(blockCoordsMins.x, blockCoordsMins.y, blockCoordsMaxs.z);
			vertexFace.vertexes[2] = vertex;
			vertex.m_texCoords = Vec2(sideTexCoordsMins.x, sideTexCoordsMins.y);
			vertex.m_position = Vec3(blockCoordsMins.x, blockCoordsMaxs.y, blockCoordsMaxs.z);
			vertexFace.vertexes[3] = vertex;
			out_vertexFaceArray.push_back(vertexFace);
		}
	}
	else if (m_chunkToWest){
		BlockIndex westBlockIndex = blockIndex | BLOCKINDEX_X_MASK;
		const Block& westBlock = m_chunkToWest->m_blocks[westBlockIndex];
		if (!g_blockDefinitions[westBlock.m_type].m_isOpaque && westBlock.m_type != type){
			unsigned char lighting = westBlock.GetLightValue() << 4;
			vertex.m_color = RGBAchars(lighting, lighting, lighting);

			vertex.m_texCoords = Vec2(sideTexCoordsMins.x, sideTexCoordsMaxs.y);
			vertex.m_position = Vec3(blockCoordsMins.x, blockCoordsMaxs.y, blockCoordsMins.z);
			vertexFace.vertexes[0] = vertex;
			vertex.m_texCoords = Vec2(sideTexCoordsMaxs.x, sideTexCoordsMaxs.y);
			vertex.m_position = Vec3(blockCoordsMins.x, blockCoordsMins.y, blockCoordsMins.z);
			vertexFace.vertexes[1] = vertex;
			vertex.m_texCoords = Vec2(sideTexCoordsMaxs.x, sideTexCoordsMins.y);
			vertex.m_position = Vec3(blockCoordsMins.x, blockCoordsMins.y, blockCoordsMaxs.z);
			vertexFace.vertexes[2] = vertex;
			vertex.m_texCoords = Vec2(sideTexCoordsMins.x, sideTexCoordsMins.y);
			vertex.m_position = Vec3(blockCoordsMins.x, blockCoordsMaxs.y, blockCoordsMaxs.z);
			vertexFace.vertexes[3] = vertex;
			out_vertexFaceArray.push_back(vertexFace);
		}
	}
}

///=====================================================
/// 
///=====================================================
void Chunk::AddWeatherVertexesToRenderingArray(const Block& block, BlockIndex blockIndex, Vertex3D_PCT_Faces& out_vertexFaceArray, const Vec2& camForwardNormal, bool isSnow) const{
	const WorldCoords blockCoordsMins = GetWorldCoordsAtIndex(blockIndex);
	const WorldCoords blockCoordsMaxs = blockCoordsMins + Vec3(1.0f, 1.0f, 1.0f);

	Vertex3D_PCT vertex;
	Vertex3D_PCT_Face vertexFace;

	float fallSpeed;
	if (isSnow)
		fallSpeed = 0.2f;
	else
		fallSpeed = 4.0f;
	const Vec2 weatherTexCoordsMins(GetPseudoRandomNoiseValueZeroToOne2D((int)blockCoordsMins.x, (int)blockCoordsMins.y), GetPseudoRandomNoiseValueZeroToOne2D((int)blockCoordsMaxs.y, (int)blockCoordsMaxs.x) + 0.5f * blockCoordsMins.z - fallSpeed * (float)GetCurrentSeconds());
	const Vec2 weatherTexCoordsMaxs = weatherTexCoordsMins + Vec2(1.0f, 0.5f);

	//scroll texCoords

	const Vec2 weatherWorldCoordMins(blockCoordsMins.x + 0.5f - 0.5f * camForwardNormal.y, blockCoordsMins.y + 0.5f + 0.5f * camForwardNormal.x);
	const Vec2 weatherWorldCoordMaxs(blockCoordsMins.x + 0.5f + 0.5f * camForwardNormal.y, blockCoordsMins.y + 0.5f - 0.5f * camForwardNormal.x);

	unsigned char lighting = block.GetLightValue() << 4;
	vertex.m_color = RGBAchars(lighting, lighting, lighting);

	vertex.m_texCoords = Vec2(weatherTexCoordsMins.x, weatherTexCoordsMaxs.y);
	vertex.m_position = Vec3(weatherWorldCoordMins.x, weatherWorldCoordMins.y, blockCoordsMins.z);
	vertexFace.vertexes[0] = vertex;
	vertex.m_texCoords = Vec2(weatherTexCoordsMaxs.x, weatherTexCoordsMaxs.y);
	vertex.m_position = Vec3(weatherWorldCoordMaxs.x, weatherWorldCoordMaxs.y, blockCoordsMins.z);
	vertexFace.vertexes[1] = vertex;
	vertex.m_texCoords = Vec2(weatherTexCoordsMaxs.x, weatherTexCoordsMins.y);
	vertex.m_position = Vec3(weatherWorldCoordMaxs.x, weatherWorldCoordMaxs.y, blockCoordsMaxs.z);
	vertexFace.vertexes[2] = vertex;
	vertex.m_texCoords = Vec2(weatherTexCoordsMins.x, weatherTexCoordsMins.y);
	vertex.m_position = Vec3(weatherWorldCoordMins.x, weatherWorldCoordMins.y, blockCoordsMaxs.z);
	vertexFace.vertexes[3] = vertex;

	out_vertexFaceArray.push_back(vertexFace);
}

///=====================================================
/// 
///=====================================================
void Chunk::DirtyEastBorderNonopaqueBlocks(BlockLocations& dirtyBlocksList){
	for (int colStart = BLOCKS_PER_CHUNK_X - 1; colStart < BLOCKS_PER_CHUNK; colStart += BLOCKS_PER_CHUNK_LAYER){
		for (BlockIndex index = (BlockIndex)(colStart); index < colStart + BLOCKS_PER_CHUNK_LAYER; index += BLOCKS_PER_CHUNK_X){
			Block& block = m_blocks[index];
			if (!g_blockDefinitions[block.m_type].m_isOpaque && !block.IsLightingDirty()){
				block.DirtyLighting();
				BlockLocation blockLocation(this, index);
				if (g_debugPointsEnabled)
					g_debugPositions.push_back(GetWorldCoordsAtIndex(index));
				dirtyBlocksList.push_back(blockLocation);
			}
		}
	}
}

///=====================================================
/// 
///=====================================================
void Chunk::DirtyWestBorderNonopaqueBlocks(BlockLocations& dirtyBlocksList){
	for (int colStart = 0; colStart < BLOCKS_PER_CHUNK; colStart += BLOCKS_PER_CHUNK_LAYER){
		for (BlockIndex index = (BlockIndex)(colStart); index < colStart + BLOCKS_PER_CHUNK_LAYER; index += BLOCKS_PER_CHUNK_X){
			Block& block = m_blocks[index];
			if (!g_blockDefinitions[block.m_type].m_isOpaque && !block.IsLightingDirty()){
				block.DirtyLighting();
				BlockLocation blockLocation(this, index);
				if (g_debugPointsEnabled)
					g_debugPositions.push_back(GetWorldCoordsAtIndex(index));
				dirtyBlocksList.push_back(blockLocation);
			}
		}
	}
}

///=====================================================
/// 
///=====================================================
void Chunk::DirtyNorthBorderNonopaqueBlocks(BlockLocations& dirtyBlocksList){
	for (int rowStart = BLOCKS_PER_CHUNK_LAYER - BLOCKS_PER_CHUNK_X; rowStart < BLOCKS_PER_CHUNK; rowStart += BLOCKS_PER_CHUNK_LAYER){
		for (BlockIndex index = (BlockIndex)(rowStart); index < rowStart + BLOCKS_PER_CHUNK_X; ++index){
			Block& block = m_blocks[index];
			if (!g_blockDefinitions[block.m_type].m_isOpaque && !block.IsLightingDirty()){
				block.DirtyLighting();
				BlockLocation blockLocation(this, index);
				if (g_debugPointsEnabled)
					g_debugPositions.push_back(GetWorldCoordsAtIndex(index));
				dirtyBlocksList.push_back(blockLocation);
			}
		}
	}
}

///=====================================================
/// 
///=====================================================
void Chunk::DirtySouthBorderNonopaqueBlocks(BlockLocations& dirtyBlocksList){
	for (int rowStart = 0; rowStart < BLOCKS_PER_CHUNK; rowStart += BLOCKS_PER_CHUNK_LAYER){
		for (BlockIndex index = (BlockIndex)(rowStart); index < rowStart + BLOCKS_PER_CHUNK_X; ++index){
			Block& block = m_blocks[index];
			if (!g_blockDefinitions[block.m_type].m_isOpaque && !block.IsLightingDirty()){
				block.DirtyLighting();
				BlockLocation blockLocation(this, index);
				if (g_debugPointsEnabled)
					g_debugPositions.push_back(GetWorldCoordsAtIndex(index));
				dirtyBlocksList.push_back(blockLocation);
			}
		}
	}
}

///=====================================================
/// 
///=====================================================
bool Chunk::IsInFrontOfCamera(const Vec3& camPosition, const Vec3& camForward) const{
	if (DotProduct(camForward, m_worldCoordsMins - camPosition) > 0.0f)
		return true;
	if (DotProduct(camForward, m_worldCoordsMins + Vec3(0.0f, (float)BLOCKS_PER_CHUNK_Y, 0.0f) - camPosition) > 0.0f)
		return true;
	if (DotProduct(camForward, m_worldCoordsMins + Vec3((float)BLOCKS_PER_CHUNK_X, 0.0f, 0.0f) - camPosition) > 0.0f)
		return true;
	if (DotProduct(camForward, m_worldCoordsMins + Vec3((float)BLOCKS_PER_CHUNK_X, (float)BLOCKS_PER_CHUNK_Y, 0.0f) - camPosition) > 0.0f)
		return true;
	if (DotProduct(camForward, m_worldCoordsMins + Vec3(0.0f, 0.0f, (float)BLOCKS_PER_CHUNK_Z) - camPosition) > 0.0f)
		return true;
	if (DotProduct(camForward, m_worldCoordsMins + Vec3(0.0f, (float)BLOCKS_PER_CHUNK_Y, (float)BLOCKS_PER_CHUNK_Z) - camPosition) > 0.0f)
		return true;
	if (DotProduct(camForward, m_worldCoordsMins + Vec3((float)BLOCKS_PER_CHUNK_X, 0.0f, (float)BLOCKS_PER_CHUNK_Z) - camPosition) > 0.0f)
		return true;
	if (DotProduct(camForward, m_worldCoordsMins + Vec3((float)BLOCKS_PER_CHUNK_X, (float)BLOCKS_PER_CHUNK_Y, (float)BLOCKS_PER_CHUNK_Z) - camPosition) > 0.0f)
		return true;
	return false;
}

///=====================================================
/// 
///=====================================================
float Chunk::CalculateWeatherAtWorldCoords(const WorldCoords& worldCoords){
	float currentSeconds = (float)GetCurrentSeconds();
	return 0.5f + ComputePerlinNoiseValueAtPosition2D(Vec2(worldCoords.x - 1.5f * currentSeconds, worldCoords.y), 300.0f, 8, 0.5f, 0.5f);
}

///=====================================================
/// 
///=====================================================
float Chunk::CalculateBiomeAtWorldCoords(const WorldCoords& worldCoords, int& out_groundHeightForColumn){
	out_groundHeightForColumn = (int)(AVERAGE_GROUND_HEIGHT + ComputePerlinNoiseValueAtPosition2D(Vec2(worldCoords.x, worldCoords.y), 80.0f, 8, 18.0f, 0.5f));
	float biome = 0.5f + ComputePerlinNoiseValueAtPosition2D(Vec2(worldCoords.x, worldCoords.y), 200.0f, 8, 0.5f, 0.5f);
	biome *= sqrt((float)out_groundHeightForColumn * (1.0f / AVERAGE_GROUND_HEIGHT));
	return biome;
}

///=====================================================
/// 
///=====================================================
bool Chunk::IsRainingAtWorldCoords(const WorldCoords& worldCoords){
	int unused;
	if (CalculateWeatherAtWorldCoords(worldCoords) >= PERLIN_MINIMUM_PRECIPITATION && CalculateBiomeAtWorldCoords(worldCoords, unused) < PERLIN_MINIMUM_SNOW_BIOME)
		return true;
	return false;
}
