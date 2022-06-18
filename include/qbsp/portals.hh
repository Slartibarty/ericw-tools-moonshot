/*
    Copyright (C) 1996-1997  Id Software, Inc.
    Copyright (C) 1997       Greg Lewis

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA

    See file, 'COPYING', for details.
*/

#pragma once

#include <common/bspfile.hh>
#include <qbsp/qbsp.hh>

struct portal_t
{
    int planenum;
    node_t *onnode; // nullptr = portal to the outside of the world (one of six sides of a box)
    node_t *nodes[2]; // [0] = front side of planenum
    portal_t *next[2]; // [0] = next portal in nodes[0]'s list of portals
    std::optional<winding_t> winding;
};

struct tree_t
{
    node_t *headnode;
    node_t outside_node = {}; // portals outside the world face this
    aabb3d bounds;
};

class portal_state_t
{
public:
    int num_visportals;
    int num_visleafs; // leafs the player can be in
    int num_visclusters; // clusters of leafs
    int iNodesDone;
    bool uses_detail;
};

struct portalstats_t {
    std::atomic<int> c_tinyportals;
};

contentflags_t ClusterContents(const node_t *node);
void MakeNodePortal(node_t *node, portalstats_t &stats);
void SplitNodePortals(node_t *node, portalstats_t &stats);
void MakeTreePortals(tree_t *tree);
void FreeTreePortals_r(node_t *node);
void AssertNoPortals(node_t *node);
void MakeHeadnodePortals(tree_t *tree);
void CutNodePortals_r(node_t *node, portal_state_t *state);
