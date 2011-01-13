/*
  Copyright (c) 2010, The Mineserver Project
  All rights reserved.

  Redistribution and use in source and binary forms, with or without
  modification, are permitted provided that the following conditions are met:
  * Redistributions of source code must retain the above copyright
    notice, this list of conditions and the following disclaimer.
  * Redistributions in binary form must reproduce the above copyright
    notice, this list of conditions and the following disclaimer in the
    documentation and/or other materials provided with the distribution.
  * Neither the name of the The Mineserver Project nor the
    names of its contributors may be used to endorse or promote products
    derived from this software without specific prior written permission.

  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
  ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
  WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
  DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER BE LIABLE FOR ANY
  DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
  (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
  LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
  ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
  (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
  SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifdef WIN32
  #include <conio.h>
  #include <direct.h>
  #include <winsock2.h>
#else
  #include <netinet/in.h>
#endif

#include <string.h>
#include <cstdlib>
#include <iostream>
#include <vector>
#include <cstdio>
#include <ctime>
#include <cmath>
#include <algorithm>

#include <zlib.h>
#include <sys/stat.h>

#include "logger.h"
#include "tools.h"
#include "map.h"
#include "worldgen/mapgen.h"
#include "user.h"
#include "nbt.h"
#include "config.h"
#include "permissions.h"
#include "chat.h"
#include "mineserver.h"
#include "tree.h"

Map::Map()
{
  for(int i = 0; i < 256; i++)
  {
    emitLight[i] = 0;
  }
  emitLight[0x0A] = 15; // Lava
  emitLight[0x0B] = 15; // Stationary Lava
  emitLight[0x27] = 1;  // Brown mushroom
  emitLight[0x32] = 14; // Torch
  emitLight[0x33] = 15; // Fire
  emitLight[0x3E] = 14; // Lit furnace
  emitLight[0x4A] = 9;  // Redstone ore (Glowing)
  emitLight[0x4C] = 7;  // Redstone Torch (On)
  emitLight[0x59] = 15; // Lightstone
  emitLight[0x5A] = 11; // Portal
  emitLight[0x5B] = 15; // Jack-O-Lantern

  for(int i = 0; i < 256; i++)
  {
    stopLight[i] = 16;
  }
  stopLight[0x00] = 0; // Empty
  stopLight[0x06] = 0; // Sapling
  stopLight[0x08] = 3; // Water
  stopLight[0x09] = 3; // Stationary water
  stopLight[0x12] = 3; // Leaves
  stopLight[0x14] = 0; // Glass
  stopLight[0x25] = 0; // Yellow flower
  stopLight[0x26] = 0; // Red rose
  stopLight[0x27] = 0; // Brown mushroom
  stopLight[0x28] = 0; // Red mushroom
  stopLight[0x32] = 0; // Torch
  stopLight[0x33] = 0; // Fire
  stopLight[0x34] = 0; // Mob spawner
  stopLight[0x35] = 0; // Wooden stairs
  stopLight[0x37] = 0; // Redstone wire
  stopLight[0x40] = 0; // Wooden door
  stopLight[0x41] = 0; // Ladder
  stopLight[0x42] = 0; // Minecart track
  stopLight[0x43] = 0; // Cobblestone stairs
  stopLight[0x47] = 0; // Iron door
  stopLight[0x4b] = 0; // Redstone Torch (Off)
  stopLight[0x4C] = 0; // Redstone Torch (On)
  stopLight[0x4e] = 0; // Snow
  stopLight[0x4f] = 3; // Ice
  stopLight[0x55] = 0; // Fence
  stopLight[0x5A] = 0; // Portal
  stopLight[0x5B] = 0; // Jack-O-Lantern
  stopLight[BLOCK_SIGN_POST] = 0; // Sign post
  stopLight[BLOCK_WALL_SIGN] = 0; // Wall sign
}

Map::~Map()
{
  // Free chunk memory
  for(std::map<uint32_t, sChunk>::const_iterator it = maps.begin(); it != maps.end(); it = maps.begin())
  {
    releaseMap(maps[it->first].x, maps[it->first].z);
  }

  //Free item memory
  for(std::map<uint32_t, spawnedItem *>::const_iterator it = items.begin(); it != items.end(); ++it)
  {
    delete items[it->first];
  }

  items.clear();
  std::string infile = mapDirectory+"/level.dat";

  NBT_Value* root = NBT_Value::LoadFromFile(infile);
  if(root != NULL)
  {
    NBT_Value &data = *((*root)["Data"]);

    //Get time from the map
    *data["Time"] = mapTime;

    NBT_Value* trees = ((*root)["Trees"]);

    if(trees)
    {
      std::vector<NBT_Value*>* tree_vec = trees->GetList();

      tree_vec->clear();

      for(std::list<sTree>::iterator iter = saplings.begin(); iter != saplings.end(); ++iter)
      {
        //(*trees)[i] = (*iter)
        NBT_Value* tree = new NBT_Value(NBT_Value::TAG_COMPOUND);
        tree->Insert("X", new NBT_Value( (int32_t)(*iter).x));
        tree->Insert("Y", new NBT_Value( (int32_t)(*iter).y));
        tree->Insert("Z", new NBT_Value( (int32_t)(*iter).z));
        tree->Insert("plantedTime", new NBT_Value( (int32_t)(*iter).plantedTime));
        tree->Insert("plantedBy", new NBT_Value( (int32_t)(*iter).plantedBy));
        tree_vec->push_back(tree);
      }
    }

    root->SaveToFile(infile);

    delete root;
  }
}

void Map::addSapling(User* user, int x, int y, int z)
{
  LOG(INFO, "Map", "Place sapling " + dtos(x) + " " + dtos(y) + " " + dtos(z));

  saplings.push_back( sTree(x,y,z,mapTime,user->UID) );
}

void Map::checkGenTrees()
{
  std::list<sTree>::iterator iter = saplings.begin();

  while (iter != saplings.end())
  {
    if(rand() % 50 == 0)
    {
      LOG(INFO, "Map", "Grow tree!");

      int32_t x = (*iter).x;
      int32_t y = (*iter).y;
      int32_t z = (*iter).z;

      Tree tree(x,y,z);
      tree.generate();

      saplings.erase(iter++);  // alternatively, i = items.erase(i);
    }
    else
    {
        //other_code_involving(*i);
        ++iter;
    }
  }

}

void Map::init()
{
#ifdef _DEBUG
  LOGLF("Map::init()");
#endif
  mapDirectory = Mineserver::get()->config()->sData("map.storage.nbt.directory");
  if(mapDirectory == "Not found!")
  {
    std::cout << "Error, mapdir not defined!" << std::endl;
    exit(EXIT_FAILURE);
  }

  std::string infile = mapDirectory+"/level.dat";

  struct stat stFileInfo;
  if(stat(mapDirectory.c_str(), &stFileInfo) != 0)
  {
    LOG(WARNING, "Map","Warning: Map directory not found, creating it now.");

#ifdef WIN32
    if(_mkdir(mapDirectory.c_str()) == -1)
#else
    if(mkdir(mapDirectory.c_str(), 0755) == -1)
#endif
    {
      LOG(EMERG, "Map","Error: Could not create map directory.");

      exit(EXIT_FAILURE);
    }
  }

  if(stat((infile).c_str(), &stFileInfo) != 0)
  {
    LOG(WARNING, "Map","Warning: level.dat not found, creating it now.");

    NBT_Value level(NBT_Value::TAG_COMPOUND);
    level.Insert("Data", new NBT_Value(NBT_Value::TAG_COMPOUND));
    level["Data"]->Insert("Time", new NBT_Value((int64_t)0));
    level["Data"]->Insert("SpawnX", new NBT_Value((int32_t)0));
    level["Data"]->Insert("SpawnY", new NBT_Value((int32_t)120));
    level["Data"]->Insert("SpawnZ", new NBT_Value((int32_t)0));
    level["Data"]->Insert("RandomSeed", new NBT_Value((int64_t)(rand()*65535)));

    level.Insert("Trees", new NBT_Value(NBT_Value::TAG_LIST));

    level.SaveToFile(infile);

    if (stat(infile.c_str(), &stFileInfo) != 0)
    {
      LOG(EMERG, "Map", "Error: Could not create level.dat");
      exit(EXIT_FAILURE);
    }
  }

  NBT_Value* root = NBT_Value::LoadFromFile(infile);
  NBT_Value& data = *((*root)["Data"]);

  spawnPos.x() = (int32_t)*data["SpawnX"];
  spawnPos.y() = (int32_t)*data["SpawnY"];
  spawnPos.z() = (int32_t)*data["SpawnZ"];

  //Get time from the map
  mapTime      = (int64_t)*data["Time"];
  mapSeed      = (int64_t)*data["RandomSeed"];

  /////////////
  // Basic tree handling

  // Get list of saplings from map:
  NBT_Value* trees = ((*root)["Trees"]);

  if(!trees || trees->GetListType() != NBT_Value::TAG_COMPOUND)
  {
    LOG(INFO, "Map", "No Trees in level.dat, creating..");
    root->Insert("Trees", new NBT_Value(NBT_Value::TAG_LIST,NBT_Value::TAG_COMPOUND));
    trees = ((*root)["Trees"]);
    root->SaveToFile(infile);
  }

#ifdef _DEBUG
  std::string dump;
  trees->Dump(dump);
  LOG(DEBUG, "Map", dump);
  //Mineserver::get()->screen()->log(dtos((*tree_list).size()) + " saplings");
#endif

  std::vector<NBT_Value*>* tree_list = trees->GetList();

  for(std::vector<NBT_Value*>::iterator iter = (*tree_list).begin(); iter != (*tree_list).end(); ++iter)
  {
    NBT_Value& tree = *(*iter);
    int32_t x = (int32_t)*tree["X"];
    int32_t y = (int32_t)*tree["Y"];
    int32_t z = (int32_t)*tree["Z"];
    int32_t plantedTime = (int32_t)*tree["plantedTime"];
    int32_t plantedBy = (int32_t)*tree["plantedBy"];
    saplings.push_back( sTree(x,y,z,plantedTime,plantedBy) );
    LOG(INFO, "Map", "sapling: " + dtos(x) + " " + dtos(y) + " " + dtos(z));
  }

  /////////////////

  // Init mapgenerator
  Mineserver::get()->mapGen()->init((int32_t)mapSeed);

  delete root;
#ifdef _DEBUG
  //Mineserver::get()->screen()->log("Spawn: (" + spawnPos.x() + "," + spawnPos.y() + "," + spawnPos.z() + ")");
#endif
}

sChunk* Map::getMapData(int x, int z, bool generate)
{
#ifdef _DEBUG2
  printf("getMapData(x=%d, z=%d, generate=%d)\n", x, z, generate);
#endif

  sChunk* chunk = chunks.getChunk(x,z);

  if(chunk != NULL || generate == false)
    return chunk;

  return loadMap(x,z, generate);

}

bool Map::saveWholeMap()
{
#ifdef _DEBUG
  printf("saveWholeMap()\n");
#endif

  for(std::map<uint32_t, sChunk>::const_iterator it = maps.begin(); it != maps.end(); ++it)
  {
    saveMap(maps[it->first].x, maps[it->first].z);
  }

  /////////////////////
  // Save map details

  std::string infile = mapDirectory+"/level.dat";

  NBT_Value* root = NBT_Value::LoadFromFile(infile);
  if(root != NULL)
  {
    NBT_Value& data = *((*root)["Data"]);

    //Get time from the map
    *data["Time"] = mapTime;
    NBT_Value* trees = ((*root)["Trees"]);

    if(trees)
    {
      std::vector<NBT_Value*>* tree_vec = trees->GetList();

      tree_vec->clear();

      for(std::list<sTree>::iterator iter = saplings.begin(); iter != saplings.end(); ++iter)
      {
        //(*trees)[i] = (*iter)
        NBT_Value* tree = new NBT_Value(NBT_Value::TAG_COMPOUND);
        tree->Insert("X", new NBT_Value( (int32_t)(*iter).x));
        tree->Insert("Y", new NBT_Value( (int32_t)(*iter).y));
        tree->Insert("Z", new NBT_Value( (int32_t)(*iter).z));
        tree->Insert("plantedTime", new NBT_Value( (int32_t)(*iter).plantedTime));
        tree->Insert("plantedBy", new NBT_Value( (int32_t)(*iter).plantedBy));
        tree_vec->push_back(tree);
      }
    }
    root->SaveToFile(infile);

    delete root;
  }

  return true;
}

bool Map::generateLight(int x, int z)
{
#ifdef _DEBUG2
  printf("generateLight(x=%d, z=%d)\n", x, z);
#endif

  sChunk* chunk = chunks.getChunk(x,z);
  if(chunk == NULL)
  {
    LOGLF("Loading chunk failed (generateLight)");
    return false;
  }

  return generateLight(x, z, chunk);
}

//#define PRINT_LIGHTGEN_TIME

bool Map::generateLight(int x, int z, sChunk* chunk)
{
#ifdef _DEBUG2
  LOGLF("generateLight(x=%d, z=%d, chunk=%p)\n", x, z, chunk);
#endif
  #ifdef PRINT_LIGHTGEN_TIME
  #ifdef WIN32
     DWORD t_begin,t_end;
     t_begin = timeGetTime();
  #else
    clock_t t_begin,t_end;
    t_begin = clock();
  #endif
  #endif

  uint8_t* blocks     = chunk->blocks;
  uint8_t* skylight   = chunk->skylight;
  uint8_t* blocklight = chunk->blocklight;
  uint8_t* heightmap  = chunk->heightmap;

  int highest_y = 0;

  // Clear lightmaps
  memset(skylight, 0, 16*16*128/2);
  memset(blocklight, 0, 16*16*128/2);

  // Sky light
  int light = 0;
  bool foundheight = false;
  for(int block_x = 0; block_x < 16; block_x++)
  {
    for(int block_z = 0; block_z < 16; block_z++)
    {
      light = 15;
      foundheight = false;
      int32_t blockx_blockz=(block_z << 7) + (block_x << 11);

      for(int block_y = 127; block_y > 0; block_y--)
      {
        int index      = block_y + blockx_blockz;
        int absolute_x = x*16+block_x;
        int absolute_z = z*16+block_z;
        uint8_t block    = blocks[index];

        light -= stopLight[block];
        if (light < 0)
        {
          light = 0;
        }

        // Calculate heightmap while looping this
        if ((block != BLOCK_AIR) && (foundheight == false))
        {
          heightmap[block_z+(block_x<<4)] = ((block_y == 127) ? block_y : block_y + 1);
          foundheight = true;
        }

        if (light < 1)
        {
          if (block_y > highest_y)
          {
            highest_y = block_y;
          }

          break;
        }

        setLight(absolute_x, block_y, absolute_z, light, 0, 1, chunk);
      }
    }
  }

  // Block light
  for (int block_x = 0; block_x < 16; block_x++)
  {
    for (int block_z = 0; block_z < 16; block_z++)
    {
      int32_t blockx_blockz=(block_z << 7) + (block_x << 11);
      for (int block_y = highest_y; block_y >= 0; block_y--)
      {
        int index      = block_y + blockx_blockz;
        int absolute_x = x*16+block_x;
        int absolute_z = z*16+block_z;
        uint8_t block    = blocks[index];

        // If light emitting block
        if(emitLight[block] > 0)
        {
          setLight(absolute_x, block_y, absolute_z, 0, emitLight[block], 2, chunk);
        }
      }
    }
  }

  // Spread light
  for (int block_x = 0; block_x < 16; block_x++)
  {
    for (int block_z = 0; block_z < 16; block_z++)
    {
      for (int block_y = heightmap[block_z+(block_x<<4)]; block_y >= 0; block_y--)
      {
        int absolute_x = x*16+block_x;
        int absolute_z = z*16+block_z;
        uint8_t skylight_s, blocklight_s;

        getLight(absolute_x, block_y, absolute_z, &skylight_s, &blocklight_s, chunk);

        if (skylight_s || blocklight_s)
        {
          spreadLight(absolute_x, block_y, absolute_z, skylight_s, blocklight_s);
        }
      }
    }
  }
  #ifdef PRINT_LIGHTGEN_TIME
  #ifdef WIN32
    t_end = timeGetTime ();
    Mineserver::get()->screen()->log("Lightgen: " + dtos(t_end-t_begin) + "ms");
  #else
    t_end = clock();
    Mineserver::get()->screen()->log("Lightgen: " + dtos((t_end-t_begin)/(CLOCKS_PER_SEC/1000))) + "ms");
  #endif
  #endif


  return true;
}

bool Map::spreadLight(int x, int y, int z, int skylight, int blocklight)
{
#ifdef _DEBUG2
  printf("spreadLight(x=%d, y=%d, z=%d, skylight=%d, blocklight=%d)\n", x, y, z, skylight, blocklight);
#endif

  if((y < 0) || (y > 127))
  {
    LOGLF("Invalid y value (spreadLight)");
    return false;
  }

  int chunk_x = blockToChunk(x);
  int chunk_z = blockToChunk(z);

  sChunk* chunk = getMapData(chunk_x, chunk_z, false);

  if(!chunk)
  {
    LOGLF("Loading chunk failed (spreadLight)");
    return false;
  }

  return spreadLight(x, y, z, skylight, blocklight, chunk);
}

bool Map::spreadLight(int x, int y, int z, int skylight, int blocklight, sChunk* chunk)
{
#ifdef _DEBUG2
  printf("spreadLight(x=%d, y=%d, z=%d, skylight=%d, blocklight=%d, chunk=%p)\n", x, y, z, skylight, blocklight, chunk);
#endif

  uint8_t block, meta;

  // If no light, stop!
  if((skylight < 1) && (blocklight < 1))
    return false;

  for(int i = 0; i < 6; i++)
  {
    // Going too high
    if((y == 127) && (i == 2))
      i++;
    // going negative
    if((y == 0) && (i == 3))
      i++;

    int x_toset = x;
    int y_toset = y;
    int z_toset = z;

    switch(i)
    {
      case 0: x_toset++; break;
      case 1: x_toset--; break;
      case 2: y_toset++; break;
      case 3: y_toset--; break;
      case 4: z_toset++; break;
      case 5: z_toset--; break;
    }

    if (getBlock(x_toset, y_toset, z_toset, &block, &meta, false))
    {
      uint8_t skylightCurrent, blocklightCurrent;
      int skylightNew, blocklightNew;
      bool spread = false;

      skylightNew = skylight-stopLight[block]-1;
      if (skylightNew < 0)
      {
        skylightNew = 0;
      }

      blocklightNew = blocklight-stopLight[block]-1;
      if (blocklightNew < 0)
      {
        blocklightNew = 0;
      }

      getLight(x_toset, y_toset, z_toset, &skylightCurrent, &blocklightCurrent, chunk);

      if (skylightNew > skylightCurrent)
      {
        skylightCurrent = skylightNew;
        spread = true;
      }

      if (blocklightNew > blocklightCurrent)
      {
        blocklightCurrent = blocklightNew;
        spread = true;
      }

      if (spread)
      {
        setLight(x_toset, y_toset, z_toset, skylightCurrent, blocklightCurrent, 4, chunk);
        spreadLight(x_toset, y_toset, z_toset, skylightCurrent, blocklightCurrent, chunk);
      }
    }
  }

  return true;
}

bool Map::getBlock(int x, int y, int z, uint8_t* type, uint8_t* meta, bool generate)
{
#ifdef _DEBUG2
  printf("getBlock(x=%d, y=%d, z=%d, type=%p, meta=%p, generate=%d)\n", x, y, z, type, meta, generate);
#endif

  if((y < 0) || (y > 127))
  {
    printf("(%i, %i, %i) ", x, y, z);
    LOGLF("Invalid y value (getBlock)");
    return false;
  }

  int chunk_x = blockToChunk(x);
  int chunk_z = blockToChunk(z);

  sChunk* chunk = getMapData(chunk_x, chunk_z, generate);

  if(!chunk)
  {
    if(generate)
    {
     LOGLF("Loading chunk failed (getBlock)");
    }

    return false;
  }

  return getBlock(x, y, z, type, meta, generate, chunk);
}

bool Map::getBlock(int x, int y, int z, uint8_t* type, uint8_t* meta, bool generate, sChunk* chunk)
{
#ifdef _DEBUG2
  printf("getBlock(x=%d, y=%d, z=%d, type=%p, meta=%p, generate=%d)\n", x, y, z, type, meta, generate);
#endif

  int chunk_block_x  = blockToChunkBlock(x);
  int chunk_block_z  = blockToChunkBlock(z);

  uint8_t* blocks      = chunk->blocks;
  uint8_t* metapointer = chunk->data;
  int index          = y + (chunk_block_z << 7) + (chunk_block_x << 11);
  *type              = blocks[index];
  uint8_t metadata     = metapointer[(index)>>1];

  if(y & 1)
  {
    metadata  &= 0xf0;
    metadata >>= 4;
  }
  else
  {
    metadata &= 0x0f;
  }

  *meta              = metadata;
  chunk->lastused    = (int)time(0);

  return true;
}

bool Map::getLight(int x, int y, int z, uint8_t* skylight, uint8_t* blocklight)
{
#ifdef _DEBUG2
  printf("getLight(x=%d, y=%d, z=%d, skylight=%p, blocklight=%p)\n", x, y, z, skylight, blocklight);
#endif

  if((y < 0) || (y > 127))
  {
    LOGLF("Invalid y value (getLight)");
    return false;
  }

  // Map chunk pos from block pos
  int chunk_x = blockToChunk(x);
  int chunk_z = blockToChunk(z);

  sChunk* chunk = getMapData(chunk_x, chunk_z, false);

  if(!chunk)
  {
    LOGLF("Loading chunk failed (getLight)");
    return false;
  }

  return getLight(x, y, z, skylight, blocklight, chunk);
}

bool Map::getLight(int x, int y, int z, uint8_t* skylight, uint8_t* blocklight, sChunk* chunk)
{
#ifdef _DEBUG2
  printf("getLight(x=%d, y=%d, z=%d, skylight=%p, blocklight=%p, chunk=%p)\n", x, y, z, skylight, blocklight, chunk);
#endif

  // Which block inside the chunk
  int chunk_block_x = blockToChunkBlock(x);
  int chunk_block_z = blockToChunkBlock(z);

  uint8_t* blocklightPtr = chunk->blocklight;
  uint8_t* skylightPtr   = chunk->skylight;
  int index            = y + (chunk_block_z << 7) + (chunk_block_x << 11);
  *blocklight = blocklightPtr[(index)>>1];
  *skylight   = skylightPtr[(index)>>1];

  if(y%2)
  {
    *blocklight  &= 0xf0;
    *blocklight >>= 4;

    *skylight    &= 0xf0;
    *skylight   >>= 4;
  }
  else
  {
    *blocklight &= 0x0f;
    *skylight   &= 0x0f;
  }

  return true;
}

bool Map::setLight(int x, int y, int z, int skylight, int blocklight, int type)
{
#ifdef _DEBUG2
  printf("setLight(x=%d, y=%d, z=%d, skylight=%d, blocklight=%d, type=%d)\n", x, y, z, skylight, blocklight, type);
#endif

  if ((y < 0) || (y > 127))
  {
    LOGLF("Invalid y value (setLight)");
    return false;
  }

  int chunk_x = blockToChunk(x);
  int chunk_z = blockToChunk(z);

  sChunk* chunk = getMapData(chunk_x, chunk_z, false);

  if (!chunk)
  {
    LOGLF("Loading chunk failed (setLight)");
    return false;
  }

  return setLight(x, y, z, skylight, blocklight, type, chunk);
}

bool Map::setLight(int x, int y, int z, int skylight, int blocklight, int type, sChunk* chunk)
{
#ifdef _DEBUG2
  printf("setLight(x=%d, y=%d, z=%d, skylight=%d, blocklight=%d, type=%d, chunk=%p)\n", x, y, z, skylight, blocklight, type, chunk);
#endif

  int chunk_block_x        = blockToChunkBlock(x);
  int chunk_block_z        = blockToChunkBlock(z);

  uint8_t* blocklightPtr     = chunk->blocklight;
  uint8_t* skylightPtr       = chunk->skylight;
  int index                = y + (chunk_block_z << 7) + (chunk_block_x << 11);
  char skylight_local      = skylightPtr[index>>1];
  char blocklight_local    = blocklightPtr[index>>1];

  if (y & 1)
  {
    if (type & 0x5) // 1 or 4
    {
      skylight_local &= 0x0f;
      skylight_local |= skylight<<4;
    }

    if (type & 0x6) // 2 or 4
    {
      blocklight_local &= 0x0f;
      blocklight_local |= blocklight<<4;
    }
  }
  else
  {
    if (type & 0x5) // 1 or 4
    {
      skylight_local &= 0xf0;
      skylight_local |= skylight;
    }

    if (type & 0x6) // 2 or 4
    {
      blocklight_local &= 0xf0;
      blocklight_local |= blocklight;
    }
  }

  if (type & 0x5) // 1 or 4
  {
    skylightPtr[index>>1] = skylight_local;
  }

  if (type & 0x6) // 2 or 4
  {
    blocklightPtr[index>>1] = blocklight_local;
  }

  return true;
}

bool Map::setBlock(int x, int y, int z, char type, char meta)
{
#ifdef _DEBUG2
  printf("setBlock(x=%d, y=%d, z=%d, type=%d, char=%d)\n", x, y, z, type, meta);
#endif

  if((y < 0) || (y > 127))
  {
    LOGLF("Invalid y value (setBlock)");
    return false;
  }

  // Map chunk pos from block pos
  int chunk_x = blockToChunk(x);
  int chunk_z = blockToChunk(z);

  sChunk* chunk = getMapData(chunk_x, chunk_z, true);

  if(!chunk)
  {
    LOGLF("Loading chunk failed (setBlock)");
    return false;
  }

  // Which block inside the chunk
  int chunk_block_x  = blockToChunkBlock(x);
  int chunk_block_z  = blockToChunkBlock(z);

  uint8_t* blocks      = chunk->blocks;
  uint8_t* metapointer = chunk->data;
  int index          = y + (chunk_block_z << 7) + (chunk_block_x << 11);
  blocks[index] = type;
  char metadata      = metapointer[index>>1];

  if(y & 1)
  {
    metadata &= 0x0f;
    metadata |= meta<<4;
  }
  else
  {
    metadata &= 0xf0;
    metadata |= meta;
  }
  metapointer[index >> 1] = metadata;

  chunk->changed       = true;
  chunk->lightRegen    = true;
  chunk->lastused      = (int)time(NULL);

  if (type == BLOCK_AIR) 
  {
    uint8_t temp_type = 0, temp_meta = 0;
    int8_t temp_y = y;
    while (getBlock(x, temp_y, z, &temp_type, &temp_meta, false) && (temp_type == BLOCK_AIR))
    {
      temp_y--;
    }
    // We've actually moved down past the last air block to the one beneath, so we need to go back up one
    temp_y++;

    std::map<uint32_t,spawnedItem*>::const_iterator it_a = items.begin(), it_b = items.end();
    for (;it_a!=it_b;++it_a)
    {
      if ((floor(static_cast<double>(it_a->second->pos.x())/32) == x) && (floor(static_cast<double>(it_a->second->pos.y())/32) == y+1) && (floor(static_cast<double>(it_a->second->pos.z())/32) == z))
      {
        it_a->second->pos.y() = temp_y*32;
      }
    }
  }

  return true;
}

bool Map::sendBlockChange(int x, int y, int z, char type, char meta)
{
#ifdef _DEBUG2
  printf("sendBlockChange(x=%d, y=%d, z=%d, type=%d, meta=%d)\n", x, y, z, type, meta);
#endif

  Packet pkt;
  pkt << PACKET_BLOCK_CHANGE << (int32_t)x << (int8_t)y << (int32_t)z << (int8_t)type << (int8_t)meta;

  sChunk* chunk = chunks.getChunk(blockToChunk(x), blockToChunk(z));
  if(chunk == NULL)
    return false;

  chunk->sendPacket(pkt);

  return true;
}

bool Map::sendPickupSpawn(spawnedItem item)
{
  //Push to global item storage
  spawnedItem* storedItem = new spawnedItem;
  *storedItem     = item;
  items[item.EID] = storedItem;

  //Push to local item storage
  int chunk_x = blockToChunk(item.pos.x()/32);
  int chunk_z = blockToChunk(item.pos.z()/32);

  sChunk* chunk = chunks.getChunk(chunk_x, chunk_z);
  if(chunk == NULL)
    return false;

  chunk->items.push_back(storedItem);

  Packet pkt;
  pkt << PACKET_PICKUP_SPAWN << (int32_t)item.EID << (int16_t)item.item << (int8_t)item.count
      << (int32_t)item.pos.x() << (int32_t)item.pos.y() << (int32_t)item.pos.z()
      << (int8_t)0 << (int8_t)0 << (int8_t)0;

  chunk->sendPacket(pkt);

  return true;
}

void Map::createPickupSpawn(int x, int y, int z, int type, int count, int health, User *user)
{
  spawnedItem item;
  item.EID      = generateEID();
  item.health   = health;
  item.item     = type;
  item.count    = count;
  if(user != NULL)
  {
    item.spawnedBy = user->UID;
  }
  item.spawnedAt = time(NULL);

  // Check to see if the block we're being asked to put the pickup spawn in is air
  // If so, find the lowest air block at x,*,z that we can place it in and move it there
  // Effectively we get... Gravity!
  uint8_t temp_type = 0, temp_meta = 0;
  int8_t temp_y = y;
  if (getBlock(x, temp_y, z, &temp_type, &temp_meta, false) && (temp_type == BLOCK_AIR))
  {
    while (getBlock(x, temp_y, z, &temp_type, &temp_meta, false) && (temp_type == BLOCK_AIR))
    {
      temp_y--;
    }
    // We've actually moved down past the last air block, so we need to go back up one
    temp_y++;

    y = temp_y;
  }

  item.pos.x()  = x*32;
  item.pos.y()  = y*32;
  item.pos.z()  = z*32;
  //Randomize spawn position a bit
  //item.pos.x() += 5+(rand()%22);
  //item.pos.z() += 5+(rand()%22);
  item.pos.x() += 15;
  item.pos.z() += 15;

  sendPickupSpawn(item);
}

sChunk*  Map::loadMap(int x, int z, bool generate)
{
#ifdef _DEBUG2
  printf("loadMap(x=%d, z=%d, generate=%d)\n", x, z, generate);
#endif

  sChunk* chunk = chunks.getChunk(x,z);
  if(chunk != NULL)
  {
    return chunk;
  }

  chunk = new sChunk;

  // Generate map file name

  int mapposx = x;
  int modulox = mapposx & 0x3F;

  int mapposz = z;
  int moduloz = mapposz & 0x3F;

  std::string infile = mapDirectory+"/"+base36_encode(modulox)+"/"+base36_encode(moduloz)+"/c."+base36_encode(mapposx)+"."+base36_encode(mapposz)+".dat";

  struct stat stFileInfo;
  if (stat(infile.c_str(), &stFileInfo) != 0)
  {
    // If generate (false only for lightmapgenerator)
    if(generate)
    {
      Mineserver::get()->mapGen()->generateChunk(x,z);
      generateLight(x, z);

      chunk->lightRegen = false;

      //If we generated spawn pos, make sure the position is not underground!
      if(x == blockToChunk(spawnPos.x()) && z == blockToChunk(spawnPos.z()))
      {
        uint8_t block,meta;
        bool foundLand = false;
        if(getBlock(spawnPos.x(),spawnPos.y(),spawnPos.z(), &block, &meta, false) && block == BLOCK_AIR)
        {
          uint8_t new_y;
          for(new_y = spawnPos.y(); new_y > 30; new_y--)
          {
            if(getBlock(spawnPos.x(), new_y, spawnPos.z(), &block, &meta, false) && block != BLOCK_AIR)
            {
              foundLand = true;
              break;
            }
          }
          if(foundLand)
          {
            spawnPos.y() = new_y+1;

            std::string infile = mapDirectory+"/level.dat";

            NBT_Value* root = NBT_Value::LoadFromFile(infile);
            if(root != NULL)
            {
              NBT_Value& data = *((*root)["Data"]);
              *data["SpawnX"] = (int32_t)spawnPos.x();
              *data["SpawnY"] = (int32_t)spawnPos.y();
              *data["SpawnZ"] = (int32_t)spawnPos.z();

              root->SaveToFile(infile);

              delete root;
            }
          }
        }

      }

      return chunks.getChunk(x,z);
    }
    else
    {
      return NULL;
    }

  }

  chunk = new sChunk();

  chunk->nbt = NBT_Value::LoadFromFile(infile.c_str());


  if(chunk->nbt == NULL)
  {
    LOGLF("Error in loading map (unable to load file)");
    return NULL;
  }

  NBT_Value& level = *(*chunk->nbt)["Level"];

  chunk->x = (int32_t)(*level["xPos"]);
  chunk->z = (int32_t)(*level["zPos"]);

  if(chunk->x != x || chunk->z != z)
  {
    LOGLF("Error in loading map (incorrect chunk)");
    delete chunk->nbt;
    delete chunk;
    return NULL;
  }

  std::vector<uint8_t>* blocks = level["Blocks"]->GetByteArray();
  std::vector<uint8_t>* data = level["Data"]->GetByteArray();
  std::vector<uint8_t>* blocklight = level["BlockLight"]->GetByteArray();
  std::vector<uint8_t>* skylight = level["SkyLight"]->GetByteArray();
  std::vector<uint8_t>* heightmap = level["HeightMap"]->GetByteArray();

  if(blocks == 0 || data == 0 || blocklight == 0 || skylight == 0 || heightmap == 0)
  {
    LOGLF("Error in loading map (chunk missing data)");
    delete chunk->nbt;
    delete chunk;
    return NULL;
  }

  size_t fullLen = (16 * 128 * 16);
  size_t halfLen = fullLen >> 1;

  if(blocks->size() != fullLen ||
    data->size() != halfLen ||
    blocklight->size() != halfLen ||
    skylight->size() != halfLen)
  {
    LOGLF("Error in loading map (corrupt?)");
  delete chunk->nbt;
  delete chunk;
    return NULL;
  }

  chunk->blocks = &((*blocks)[0]);
  chunk->data = &((*data)[0]);
  chunk->blocklight = &((*blocklight)[0]);
  chunk->skylight = &((*skylight)[0]);
  chunk->heightmap = &((*heightmap)[0]);

  chunks.linkChunk(chunk, x, z);

  // Update last used time
  chunk->lastused = (int)time(0);

  // Not changed
  chunk->changed    = false;
  chunk->lightRegen = false;
    
  //Get list of chests,furnaces etc on the chunk
  NBT_Value* entityList = level["TileEntities"];

  //Verify the type
  if(entityList && entityList->GetType() == NBT_Value::TAG_LIST && entityList->GetListType() == NBT_Value::TAG_COMPOUND)
  {
    std::vector<NBT_Value*>* entities = entityList->GetList();
    std::vector<NBT_Value*>::iterator iter = entities->begin(), end = entities->end();

    for( ; iter != end ; iter++)
    {
      std::vector<uint8_t> buffer;
      NBT_Value* idVal = (**iter)["id"];
      if(idVal == NULL)
        continue;
      std::string* id = idVal->GetString();
      if(id == NULL)
        continue;

      if((**iter)["x"]->GetType() != NBT_Value::TAG_INT ||
         (**iter)["y"]->GetType() != NBT_Value::TAG_INT ||
         (**iter)["z"]->GetType() != NBT_Value::TAG_INT)
      {
        continue;
      }

      int32_t entityX = *(**iter)["x"];
      int32_t entityY = *(**iter)["y"];
      int32_t entityZ = *(**iter)["z"];

      if((*id=="Sign"))
      {
        signData *newSign = new signData;
        newSign->x = entityX;
        newSign->y = entityY;
        newSign->z = entityZ;
        newSign->text1=*(**iter)["Text1"]->GetString();
        newSign->text2=*(**iter)["Text2"]->GetString();
        newSign->text3=*(**iter)["Text3"]->GetString();
        newSign->text4=*(**iter)["Text4"]->GetString();

        chunk->signs.push_back(newSign);
      }
      else if((*id=="Chest"))
      {
        NBT_Value* chestItems = (**iter)["Items"];

        if(chestItems->GetType() == NBT_Value::TAG_LIST)
        {
          if(chestItems->GetListType() != NBT_Value::TAG_COMPOUND)
          {
            continue;
          }

          std::vector<NBT_Value*>* entities2 = chestItems->GetList();
          std::vector<NBT_Value*>::iterator iter2 = entities2->begin(), end2 = entities2->end();

          chestData *newChest = new chestData;
          newChest->x = entityX;
          newChest->y = entityY;
          newChest->z = entityZ;

          for( ; iter2 != end2; iter2++ )
          {
            if((**iter2)["Count"] == NULL || (**iter2)["Slot"] == NULL || (**iter2)["Damage"] == NULL || (**iter2)["id"] == NULL ||
               (**iter2)["Count"]->GetType() != NBT_Value::TAG_BYTE ||
               (**iter2)["Slot"]->GetType() != NBT_Value::TAG_BYTE ||
               (**iter2)["Damage"]->GetType() != NBT_Value::TAG_SHORT ||
               (**iter2)["id"]->GetType() != NBT_Value::TAG_SHORT)               
            {
              continue;
            }
            newChest->items[(int8_t)*(**iter2)["Slot"]].count  =  (int8_t)*(**iter2)["Count"];
            newChest->items[(int8_t)*(**iter2)["Slot"]].health = (int16_t)*(**iter2)["Damage"];
            newChest->items[(int8_t)*(**iter2)["Slot"]].type   = (int16_t)*(**iter2)["id"];
          }

          chunk->chests.push_back(newChest);
        }
      }
      else if((*id=="Furnace"))
      {
        NBT_Value* chestItems = (**iter)["Items"];

        if(chestItems->GetType() == NBT_Value::TAG_LIST)
        {
          if(chestItems->GetListType() != NBT_Value::TAG_COMPOUND)
          {
            continue;
          }

          std::vector<NBT_Value*>* entities2 = chestItems->GetList();
          std::vector<NBT_Value*>::iterator iter2 = entities2->begin(), end2 = entities2->end();

          if((**iter)["BurnTime"] == NULL || (**iter)["CookTime"] == NULL)
          {
            continue;
          }

          furnaceData *newFurnace = new furnaceData;
          newFurnace->x = entityX;
          newFurnace->y = entityY;
          newFurnace->z = entityZ;
          newFurnace->burnTime = (int16_t)*(**iter)["BurnTime"];
          newFurnace->cookTime = (int16_t)*(**iter)["CookTime"];

          for( ; iter2 != end2; iter2++ )
          {
            if((**iter2)["Count"] == NULL || (**iter)["Slot"] == NULL || (**iter)["Damage"] == NULL || (**iter)["id"] == NULL ||
               (**iter2)["Count"]->GetType() != NBT_Value::TAG_BYTE ||
               (**iter2)["Slot"]->GetType() != NBT_Value::TAG_BYTE ||
               (**iter2)["Damage"]->GetType() != NBT_Value::TAG_SHORT ||
               (**iter2)["id"]->GetType() != NBT_Value::TAG_SHORT ||
               (int8_t)*(**iter2)["Slot"] > 3 || (int8_t)*(**iter2)["Slot"] < 0)
            {
              continue;
            }
            newFurnace->items[(int8_t)*(**iter2)["Slot"]].count  = (int8_t)*(**iter2)["Count"];
            newFurnace->items[(int8_t)*(**iter2)["Slot"]].health = (int16_t)*(**iter2)["Damage"];
            newFurnace->items[(int8_t)*(**iter2)["Slot"]].type   = (int16_t)*(**iter2)["id"];
          }

          chunk->furnaces.push_back(newFurnace);          
        }
      }

      //Delete list item
      delete (*iter);
      (*iter) = NULL;
    }

    //Clear the list
    entityList->GetList()->clear();
  }

  return chunk;
}

bool Map::saveMap(int x, int z)
{
#ifdef _DEBUG2
  printf("saveMap(x=%d, z=%d)\n", x, z);
#endif

  sChunk*  chunk = chunks.getChunk(x, z);

  if(!chunk->changed)
    return true;

  // Recalculate light maps
  if(chunk->lightRegen)
  {
    generateLight(x, z, chunk);
  }

  // Generate map file name

  int mapposx = x;
  int modulox = mapposx & 0x3F;

  int mapposz = z;
  int moduloz = mapposz & 0x3F;

  std::string outfile = mapDirectory+"/"+base36_encode(modulox)+"/"+base36_encode(moduloz)+"/c."+
                        base36_encode(mapposx)+"."+base36_encode(mapposz)+".dat";

  // Try to create parent directories if necessary
  struct stat stFileInfo;
  if(stat(outfile.c_str(), &stFileInfo) != 0)
  {
    std::string outdir_a = mapDirectory+"/"+base36_encode(modulox);
    std::string outdir_b = mapDirectory+"/"+base36_encode(modulox)+"/"+base36_encode(moduloz);

    if(stat(outdir_b.c_str(), &stFileInfo) != 0)
    {
      if(stat(outdir_a.c_str(), &stFileInfo) != 0)
      {
#ifdef WIN32
        if(_mkdir(outdir_a.c_str()) == -1)
#else
        if(mkdir(outdir_a.c_str(), 0755) == -1)
#endif

          return false;
      }

#ifdef WIN32
      if(_mkdir(outdir_b.c_str()) == -1)
#else
      if(mkdir(outdir_b.c_str(), 0755) == -1)
#endif

        return false;
    }
  }


  NBT_Value* entityList = (*(*chunk->nbt)["Level"])["TileEntities"];

  if(!entityList)
  {
    entityList = new NBT_Value(NBT_Value::TAG_LIST, NBT_Value::TAG_COMPOUND);
    chunk->nbt->Insert("TileEntities", entityList);
  }

  //Save signs
  for(uint32_t i = 0; i < chunk->signs.size(); i++)
  {
    NBT_Value *val = new NBT_Value(NBT_Value::TAG_COMPOUND);
    val->Insert("id", new NBT_Value(std::string("Sign")));
    val->Insert("x", new NBT_Value((int32_t)chunk->signs[i]->x));
    val->Insert("y", new NBT_Value((int32_t)chunk->signs[i]->y));
    val->Insert("z", new NBT_Value((int32_t)chunk->signs[i]->z));
    val->Insert("Text1", new NBT_Value(chunk->signs[i]->text1));
    val->Insert("Text2", new NBT_Value(chunk->signs[i]->text2));
    val->Insert("Text3", new NBT_Value(chunk->signs[i]->text3));
    val->Insert("Text4", new NBT_Value(chunk->signs[i]->text4));

    entityList->GetList()->push_back(val);
  }

  //Save chests
  for(uint32_t i = 0; i < chunk->chests.size(); i++)
  {
    NBT_Value *val = new NBT_Value(NBT_Value::TAG_COMPOUND);
    val->Insert("id", new NBT_Value(std::string("Chest")));
    val->Insert("x", new NBT_Value((int32_t)chunk->chests[i]->x));
    val->Insert("y", new NBT_Value((int32_t)chunk->chests[i]->y));
    val->Insert("z", new NBT_Value((int32_t)chunk->chests[i]->z));
    NBT_Value* nbtInv = new NBT_Value(NBT_Value::TAG_LIST, NBT_Value::TAG_COMPOUND);
    for(uint32_t slot = 0; slot < 27; slot++)
    {
      if(chunk->chests[i]->items[slot].count && chunk->chests[i]->items[slot].type != -1)
      {
        NBT_Value* val = new NBT_Value(NBT_Value::TAG_COMPOUND);
        val->Insert("Count", new NBT_Value((int8_t)chunk->chests[i]->items[slot].count));
        val->Insert("Slot", new NBT_Value((int8_t)slot));
        val->Insert("Damage", new NBT_Value((int16_t)chunk->chests[i]->items[slot].health));
        val->Insert("id", new NBT_Value((int16_t)chunk->chests[i]->items[slot].type));
        nbtInv->GetList()->push_back(val);
      }
    }
    val->Insert("Items", nbtInv);

    entityList->GetList()->push_back(val);
  }

  //Save furnaces
  for(uint32_t i = 0; i < chunk->furnaces.size(); i++)
  {
    NBT_Value *val = new NBT_Value(NBT_Value::TAG_COMPOUND);
    val->Insert("id", new NBT_Value(std::string("Furnace")));
    val->Insert("x", new NBT_Value((int32_t)chunk->furnaces[i]->x));
    val->Insert("y", new NBT_Value((int32_t)chunk->furnaces[i]->y));
    val->Insert("z", new NBT_Value((int32_t)chunk->furnaces[i]->z));
    val->Insert("BurnTime", new NBT_Value((int16_t)chunk->furnaces[i]->burnTime));
    val->Insert("CookTime", new NBT_Value((int16_t)chunk->furnaces[i]->cookTime));
    NBT_Value* nbtInv = new NBT_Value(NBT_Value::TAG_LIST, NBT_Value::TAG_COMPOUND);

    for(uint32_t slot = 0; slot < 3; slot++)
    {
      if(chunk->furnaces[i]->items[slot].count && chunk->furnaces[i]->items[slot].type != 0 && chunk->furnaces[i]->items[slot].type != -1)
      {
        NBT_Value* val = new NBT_Value(NBT_Value::TAG_COMPOUND);
        val->Insert("Count", new NBT_Value((int8_t)chunk->furnaces[i]->items[slot].count));
        val->Insert("Slot", new NBT_Value((int8_t)slot));
        val->Insert("Damage", new NBT_Value((int16_t)chunk->furnaces[i]->items[slot].health));
        val->Insert("id", new NBT_Value((int16_t)chunk->furnaces[i]->items[slot].type));
        nbtInv->GetList()->push_back(val);
      }
    }
    val->Insert("Items", nbtInv);

    entityList->GetList()->push_back(val);
  }


  chunk->nbt->SaveToFile(outfile);

  // Set "not changed"
  chunk->changed    = false;
  chunk->lightRegen = false;

  return true;
}

bool Map::releaseMap(int x, int z)
{
  // save first
  saveMap(x, z);

  sChunk* chunk = chunks.getChunk(x,z);
  if(chunk == NULL)
    return false;

  // Erase sign data
  for(int i=0;i<chunk->signs.size();++i)
  {
    delete chunk->signs[i];
  }

  // Erase chest data
  for(int i=0;i<chunk->chests.size();++i)
  {
    delete chunk->chests[i];
  }

  // Erase furnace data
  for(int i=0;i<chunk->furnaces.size();++i)
  {
    delete chunk->furnaces[i];
  }

  chunks.unlinkChunk(x, z);
  delete chunk->nbt;
  delete chunk;

  return true;
}

// Send chunk to user
void Map::sendToUser(User* user, int x, int z)
{
#ifdef _DEBUG2
  printf("sendToUser(x=%d, z=%d)\n", x, z);
#endif

//  uint32_t mapId;
//  Map::posToId(x, z, &mapId);
  sChunk* chunk = loadMap(x, z);
  if(chunk == NULL)
    return;

  uint8_t* data4   = new uint8_t[18+81920];
  uint8_t* mapdata = new uint8_t[81920];
  int32_t mapposx    = x;
  int32_t mapposz    = z;

  //Regenerate lighting if needed
  if(chunk->lightRegen)
  {
    generateLight(x, z, chunk);
    chunk->lightRegen = false;
  }
  // Pre chunk
  user->buffer << (int8_t)PACKET_PRE_CHUNK << mapposx << mapposz << (int8_t)1;

  // Chunk
  user->buffer << (int8_t)PACKET_MAP_CHUNK << (int32_t)(mapposx * 16) << (int16_t)0 << (int32_t)(mapposz * 16)
    << (int8_t)15 << (int8_t)127 << (int8_t)15;

  memcpy(&mapdata[0], chunk->blocks, 32768);
  memcpy(&mapdata[32768], chunk->data, 16384);
  memcpy(&mapdata[32768+16384], chunk->blocklight, 16384);
  memcpy(&mapdata[32768+16384+16384], chunk->skylight, 16384);

  uLongf written = 81920;
  Bytef* buffer = new Bytef[written];

  // Compress data with zlib deflate
  compress(buffer, &written, &mapdata[0], 81920);

  user->buffer << (int32_t)written;
  user->buffer.addToWrite(buffer, written);

  //Push sign data to player
  for(uint32_t i = 0; i < chunk->signs.size(); i++)
  {
    user->buffer << (int8_t)PACKET_SIGN << chunk->signs[i]->x << (int16_t)chunk->signs[i]->y << chunk->signs[i]->z;
    user->buffer << chunk->signs[i]->text1 << chunk->signs[i]->text2 << chunk->signs[i]->text3 << chunk->signs[i]->text4;
  } 


  delete [] buffer;

  delete[] data4;
  delete[] mapdata;
}

void Map::setComplexEntity(User* user, int32_t x, int32_t y, int32_t z, NBT_Value* entity)
{
//  uint32_t mapId;

  int chunk_x = blockToChunk(x);
  int chunk_z = blockToChunk(z);

  sChunk* chunk = loadMap(chunk_x, chunk_z);
  if(chunk == NULL)
    return;

  std::string player = "";
  if(user != NULL)
  {
    player = user->nick;
  }

  int8_t locked = Mineserver::get()->config()->bData("temp.chests_locked_by_default") ? 1 : 0;

  if(entity->GetType() != NBT_Value::TAG_COMPOUND)
  {
    LOGLF("Complex Entity wasn't TAG_COMPOUND");
    return;
  }

  if((*entity)["x"] == NULL || (int32_t)*(*entity)["x"] != x  || (*entity)["y"] == NULL || (int32_t)*(*entity)["y"] != y  || (*entity)["z"] == NULL || (int32_t)*(*entity)["z"] != z)
  {
    LOGLF("Invalid Complex Entity");
    return;
  }

  NBT_Value* entityList = (*(*chunk->nbt)["Level"])["TileEntities"];

  if(!entityList)
  {
    entityList = new NBT_Value(NBT_Value::TAG_LIST, NBT_Value::TAG_COMPOUND);
    chunk->nbt->Insert("TileEntities", entityList);
  }

  if(entityList->GetType() == NBT_Value::TAG_LIST)
  {
    if(entityList->GetListType() != NBT_Value::TAG_COMPOUND)
    {
      entityList->SetType(NBT_Value::TAG_LIST, NBT_Value::TAG_COMPOUND);
    }

    std::vector<NBT_Value*>* entities = entityList->GetList();
    std::vector<NBT_Value*>::iterator iter = entities->begin(), end = entities->end();

    bool done = false;

    for( ; iter != end; iter++ )
    {
      if((**iter)["x"] == NULL || (**iter)["y"] == NULL || (**iter)["z"] == NULL ||
         (**iter)["x"]->GetType() != NBT_Value::TAG_INT ||
         (**iter)["y"]->GetType() != NBT_Value::TAG_INT ||
         (**iter)["z"]->GetType() != NBT_Value::TAG_INT)
      {
        continue;
      }

      if((int32_t)(*(**iter)["x"]) == x && (int32_t)(*(**iter)["y"]) == y && (int32_t)(*(**iter)["z"]) == z)
      {
        NBT_Value* nbtLockdata = (**iter)["Lockdata"];

        if(nbtLockdata != NULL)
        {
          player = *(*nbtLockdata)["player"]->GetString();
          locked = *(*nbtLockdata)["locked"];

          if(locked == 1)
          {
            if(!(user != NULL && player != user->nick))
            {
              return;
            }
          }
        }

        // Replace entity
        delete *iter;
        *iter = entity;
        done = true;
        break;
      }
    }

    if(!done)
    {
      if(user != NULL)
      {
        NBT_Value* nbtLockdata = new NBT_Value(NBT_Value::TAG_COMPOUND);
        nbtLockdata->Insert("player", new NBT_Value(player));
        nbtLockdata->Insert("locked", new NBT_Value(locked));
        entity->Insert("Lockdata", nbtLockdata);
      }

      // Add new entity
      entityList->GetList()->push_back(entity);
    }
  }
  else
  {
    LOGLF("TileEntities list type not valid");
    return;
  }

  // TODO: mapChanged[mapId] = true;

  std::vector<uint8_t> buffer;
  buffer.push_back(NBT_Value::TAG_COMPOUND);
  buffer.push_back(0);
  buffer.push_back(0);
  entity->Write(buffer);

  uint8_t* compressedData = new uint8_t[ALLOCATE_NBTFILE];

  z_stream zstream2;
  zstream2.zalloc = Z_NULL;
  zstream2.zfree = Z_NULL;
  zstream2.opaque = Z_NULL;
  zstream2.next_out=compressedData;
  zstream2.next_in=&buffer[0];
  zstream2.avail_in=buffer.size();
  zstream2.avail_out=ALLOCATE_NBTFILE;
  zstream2.total_out=0;
  zstream2.total_in=0;
  deflateInit2(&zstream2, Z_DEFAULT_COMPRESSION, Z_DEFLATED, 15+MAX_WBITS, 8,
             Z_DEFAULT_STRATEGY);

  //Gzip the data
  if(int state=deflate(&zstream2,Z_FULL_FLUSH)!=Z_OK)
  {
    LOG(INFO, "Map","Error in deflate: " + dtos(state));
  }

  deflateEnd(&zstream2);

  /*
  Packet pkt;
  pkt << (int8_t)PACKET_COMPLEX_ENTITIES
    << x << (int16_t)y << z << (int16_t)zstream2.total_out;
  pkt.addToWrite(compressedData, zstream2.total_out);

  chunk->sendPacket(pkt);
  */
  delete [] compressedData;

  //User::sendAll((int8_t*)pkt.getWrite(), pkt.getWriteLen());
}
