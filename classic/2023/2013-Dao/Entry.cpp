/*
 * main.cpp
 *
 * Copyright (c) 2007, Nathan Sturtevant
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in the
 *       documentation and/or other materials provided with the distribution.
 *     * Neither the name of the University of Alberta nor the
 *       names of its contributors may be used to endorse or promote products
 *       derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY NATHAN STURTEVANT ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL <copyright holder> BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <cmath>
#include <cstdlib>
#include "GenericAStar.h"
#include "MinimalSectorAbstraction.h"
#include "SearchEnvironment.h"
#include "Map.h"
#include <algorithm>
#include <map>
#include "Entry.h"

Map *m = 0;
MinimalSectorAbstraction *msa = 0;  // regular abstraction
double zoom = 21;
double targetZoom = 21;
double defaultZoom = 21;
uint32_t startx = UINT32_MAX, starty = UINT32_MAX, goalx = UINT32_MAX, goaly = UINT32_MAX;
int gCurrButton;
int gShowInfo = 1;
int gShowAbstration = 1;
uint32_t gViewMode = 1;
uint32_t gNumViews = 2;

std::vector<uint32_t> gAbstractPath;
std::vector<uint32_t> gRealPath;
std::vector<uint32_t> gAStarPath;

double DoMinimalPath(uint32_t startX, uint32_t startY,
                     uint32_t goalX, uint32_t goalY,
                     MinimalSectorAbstraction *ms,
                     std::vector<uint32_t> &abstractPath,
                     std::vector<uint32_t> &realPath);

/**
 * User code used during preprocessing of a map.  Can be left blank if no pre-processing is required.
 * It will not be called in the same program execution as `PrepareForSearch` is called,
 * all data must be shared through file.
 * 
 * Called with command below:
 * ./run -pre file.map
 * 
 * @param[in] bits Array of 2D table.  (0,0) is located at top-left corner.  bits.size() = height * width
 *                 Packed as 1D array, row-by-ray, i.e. first width bool's give row y=0, next width y=1
 *                 bits[i] returns `true` if (x,y) is traversable, `false` otherwise
 * @param[in] width Give the map's width
 * @param[in] height Give the map's height
 * @param[in] filename The filename you write the preprocessing data to.  Open in write mode.
 */
void PreprocessMap(const std::vector<bool> &bits, int width, int height, const std::string &filename) {}

/**
 * User code used to setup search before queries.  Can also load pre-processing data from file to speed load.
 * It will not be called in the same program execution as `PreprocessMap` is called,
 * all data must be shared through file.
 * 
 * Called with any commands below:
 * ./run -run file.map file.map.scen
 * ./run -check file.map file.map.scen
 * 
 * @param[in] bits Array of 2D table.  (0,0) is located at top-left corner.  bits.size() = height * width
 *                 Packed as 1D array, row-by-ray, i.e. first width bool's give row y=0, next width y=1
 *                 bits[i] returns `true` if (x,y) is traversable, `false` otherwise
 * @param[in] width Give the map's width
 * @param[in] height Give the map's height
 * @param[in] filename The filename you write the preprocessing data to.  Open in write mode.
 * @returns Pointer to data-structure used for search.  Memory should be stored on heap, not stack.
 */
void *PrepareForSearch(const std::vector<bool> &bits, int width, int height, const std::string &filename)
{
	m = new Map(width, height);
	for (int x = 0; x < width; x++)
	{
		for (int y = 0; y < height; y++)
		{
			m->SetTerrainType(x, y, bits[y*width+x]?kGround:kOutOfBounds);
		}
	}
	msa = new MinimalSectorAbstraction(m);
	return 0;
}

/**
 * User code used to setup search before queries.  Can also load pre-processing data from file to speed load.
 * It will not be called in the same program execution as `PreprocessMap` is called,
 * all data must be shared through file.
 * 
 * Called with any commands below:
 * ./run -run file.map file.map.scen
 * ./run -check file.map file.map.scen
 * 
 * @param[in,out] data Pointer to data returned from `PrepareForSearch`.  Can static_cast to correct data type.
 * @param[in] s The start (x,y) coordinate of search query
 * @param[in] g The goal (x,y) coordinate of search query
 * @param[out] path The points that forms the shortest path from `s` to `g` computed by search algorithm.
 *                  Shortest path length will calculated by summation of Euclidean distance
 *                  between consecutive pairs path[i]--path[i+1].  Collinear points are allowed.
 *                  Return an empty path if no shortest path exists.
 * @returns `true` if search is complete, including if no-path-exists.  `false` if search only partially completed.
 *          if `false` then `GetPath` will be called again until search is complete.
 */
bool GetPath(void *data, xyLoc s, xyLoc g, std::vector<xyLoc> &path)
{
	path.resize(0);
	DoMinimalPath(s.x, s.y, g.x, g.y, msa, gAbstractPath, gRealPath);
	for (unsigned int x = 0; x < gRealPath.size(); x++)
	{
		path.push_back(xyLoc{static_cast<int16_t>(gRealPath[x]>>16), static_cast<int16_t>(gRealPath[x]&0xFFFF)});
	}
	return true;
}

/**
 * The algorithm name.  Please update std::string and ensure name is immutable.
 * 
 * @returns the name of the algorithm
 */
std::string GetName()
{
	return "DAO-1-level";
}

/**
* Initialize the demo code.
 *
 * If a parameter is passed, it will try to load a file by that name as
 * a map. Begins an OpenGL visualization of the world using the GLUT
 * interface.
 *
 */
//int main(int argc, char** argv)
//{
//	printf("Loading %s\n", argv[1]);
//	m = new Map(argv[1]);
//
//	
//
//    return 0;
//}


/**
* DoMinimalPath()
 *
 * \brief Find the minimal path between two x/y locations
 *
 * First an abstract path is computed, and then the low-level path
 * is computed. This is compared to the number of nodes expanded just
 * using A*. The paths are returned by reference so they can be drawn
 * on the screen.
 *
 * Note that we don't move the region center temporarily as described
 * in the AI wisdom book, although that code is running in Dragon Age.
 */
GenericAStar gas;

double DoMinimalPath(uint32_t startX, uint32_t startY,
                     uint32_t goalX, uint32_t goalY,
                     MinimalSectorAbstraction *ms,
                     std::vector<uint32_t> &abstractPath,
                     std::vector<uint32_t> &realPath)
{
	//printf("Searching from (%d, %d) to (%d, %d)\n", startX, startY, goalX, goalY);
    if ((startX == UINT32_MAX) || (startY == UINT32_MAX) ||
        (goalX == UINT32_MAX) || (goalY == UINT32_MAX))
	{
		printf("Error; invalid start/goal\n");
        return 0;
    }

    MinimalSearchEnvironment mmse(ms);
    MapSearchEnvironment mse(m);
    
    abstractPath.resize(0);
    realPath.resize(0);
    int concNodes, absNodes, totalNodes;
    concNodes = absNodes = 0;
    totalNodes = 0;
    
    uint32_t x1 = startX;
    uint32_t y1 = startY;
    uint32_t x2 = goalX;
    uint32_t y2 = goalY;
    
    // find parent sector/region
    int region1 = ms->GetRegion(x1, y1);
    int sector1 = ms->GetSector(x1, y1);
    int region2 = ms->GetRegion(x2, y2);
    int sector2 = ms->GetSector(x2, y2);
    if ((region1 == -1) || (sector1 == -1) ||
        (region2 == -1) || (sector2 == -1))
	{
		printf("Error; sector / region:\n");
		printf("  [%d:%d] to [%d:%d]\n",
			   sector1, region1, sector2, region2);
        return 0;
    }

    // get abstract path
    gas.GetPath(&mmse,
                msaNodeData(sector1, region1).id,
                msaNodeData(sector2, region2).id, abstractPath);
    absNodes = gas.GetNodesExpanded();
    totalNodes = absNodes;
    
	/*
    printf("Abstract path from [%d:%d] to [%d:%d], %d nodes expanded\n",
           sector1, region1, sector2, region2, absNodes);
    printf("Abstract path length %d\n", (int)abstractPath.size());
	*/
    
    // replace sector/region with x/y location
    // this if for global drawing later
    for (int x = (int)abstractPath.size()-1; x >= 0; x--)
    {
        unsigned int ax, ay;
        uint32_t& apx = abstractPath[x];
        ms->GetXYLocation(msaNodeData{apx}.get_sector(), msaNodeData{apx}.get_region(), ax, ay);
        apx = ((ax<<16)|ay);
    }
    
    double dist = 0;
    // if we can't find an abstract path, there won't be a concrete one
    // but we might not have an abstract path if the start/end sector/region
    // are the same
    if ((abstractPath.size() > 0) ||
        ((sector1 == sector2) && (region1 == region2)))
    {
        // start at first abstract center, which we'll promptly skip
        int s = (int)abstractPath.size()-1;
        std::vector<uint32_t> concretePath;
        
        do {
            concretePath.resize(0);
            s-=1;
            if (s < 1) s = 0;
            if ((s > 0) && (s < (int)abstractPath.size()))
            {
                x2 = abstractPath[s]>>16;
                y2 = abstractPath[s]&0xFFFF;
            }
            else {
                x2 = goalX;
                y2 = goalY;
            }
            
            gas.GetPath(&mse, (x1<<16) | y1, (x2<<16) | y2, concretePath);
			//            printf("Computing segment from (%d, %d) to (%d, %d): %ld nodes expanded\n",
            //       x1, y1, x2, y2, gas.GetNodesExpanded());
            concNodes = concNodes>gas.GetNodesExpanded()?concNodes:gas.GetNodesExpanded();
            totalNodes += gas.GetNodesExpanded();
            
            if (concretePath.size() == 0)
                break;
            
            for (int cnt = (int)concretePath.size()-1; cnt >= 0; cnt--)
            {
                if ((realPath.size() == 0) ||
                    (realPath.back() != concretePath[cnt]))
                    realPath.push_back(concretePath[cnt]);
            }
            x1 = realPath.back()>>16;
            y1 = realPath.back()&0xFFFF;
		} while (s != 0);
	
        //printf("Complete (real) path length %d\n", (int)realPath.size());
        //printf("Total nodes expanded: %d\n", totalNodes);
    }
        
    return dist;
}
