/*
    Copyright (C) 2016       Eric Wasylishen

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

#include <qbsp/qbsp.hh>
#include <qbsp/brush.hh>
#include <qbsp/map.hh>

#include <unordered_set>
#include <fstream>
#include <fmt/ostream.h>
#include <list>

static std::ofstream InitObjFile(const std::string &filesuffix)
{
    fs::path name = qbsp_options.bsp_path;
    name.replace_filename(qbsp_options.bsp_path.stem().string() + "_" + filesuffix).replace_extension("obj");

    std::ofstream objfile(name);
    if (!objfile)
        FError("Failed to open {}: {}", qbsp_options.bsp_path, strerror(errno));

    return objfile;
}

static std::ofstream InitMtlFile(const std::string &filesuffix)
{
    fs::path name = qbsp_options.bsp_path;
    name.replace_filename(qbsp_options.bsp_path.stem().string() + "_" + filesuffix).replace_extension("mtl");

    std::ofstream file(name);
    if (!file)
        FError("Failed to open {}: {}", qbsp_options.bsp_path, strerror(errno));

    return file;
}

static void ExportObjFace(std::ofstream &f, const face_t *face, int *vertcount)
{
    const maptexinfo_t &texinfo = map.mtexinfos.at(face->texinfo);
    const char *texname = map.miptexTextureName(texinfo.miptex).c_str();

    const auto &texture = map.load_image_meta(texname);
    const int width = texture ? texture->width : 64;
    const int height = texture ? texture->height : 64;

    // export the vertices and uvs
    for (int i = 0; i < face->w.size(); i++) {
        const qvec3d &pos = face->w[i];
        fmt::print(f, "v {:.9} {:.9} {:.9}\n", pos[0], pos[1], pos[2]);

        qvec3d uv = texinfo.vecs.uvs(pos, width, height);

        // not sure why -v is needed, .obj uses (0, 0) in the top left apparently?
        fmt::print(f, "vt {:.9} {:.9}\n", uv[0], -uv[1]);
    }

    // fixme-brushbsp
    fmt::print(f, "usemtl contents{}\n", face->contents.native);
    f << 'f';
    for (int i = 0; i < face->w.size(); i++) {
        // .obj vertexes start from 1
        // .obj faces are CCW, quake is CW, so reverse the order
        const int vertindex = *vertcount + (face->w.size() - 1 - i) + 1;
        fmt::print(f, " {}/{}", vertindex, vertindex);
    }
    f << '\n';

    *vertcount += face->w.size();
}

static void WriteContentsMaterial(std::ofstream &mtlf, contentflags_t contents, float r, float g, float b)
{
    // fixme-brushbsp
    fmt::print(mtlf, "newmtl contents{}\n", contents.native);
    mtlf << "Ka 0 0 0\n";
    fmt::print(mtlf, "Kd {} {} {}\n", r, g, b);
    mtlf << "Ks 0 0 0\n";
    mtlf << "illum 0\n";
}

void ExportObj_Faces(const std::string &filesuffix, const std::vector<const face_t *> &faces)
{
    std::ofstream objfile = InitObjFile(filesuffix);
    std::ofstream mtlfile = InitMtlFile(filesuffix);

    WriteContentsMaterial(mtlfile, {}, 0, 0, 0);
    WriteContentsMaterial(mtlfile, {CONTENTS_EMPTY}, 0, 1, 0);
    WriteContentsMaterial(mtlfile, {CONTENTS_SOLID}, 0.2, 0.2, 0.2);

    WriteContentsMaterial(mtlfile, {CONTENTS_WATER}, 0.0, 0.0, 0.2);
    WriteContentsMaterial(mtlfile, {CONTENTS_SLIME}, 0.0, 0.2, 0.0);
    WriteContentsMaterial(mtlfile, {CONTENTS_LAVA}, 0.2, 0.0, 0.0);

    WriteContentsMaterial(mtlfile, {CONTENTS_SKY}, 0.8, 0.8, 1.0);
    // fixme-brushbsp
    // WriteContentsMaterial(mtlfile, {CONTENTS_SOLID, CFLAGS_CLIP}, 1, 0.8, 0.8);
    // WriteContentsMaterial(mtlfile, {CONTENTS_EMPTY, CFLAGS_HINT}, 1, 1, 1);

    // WriteContentsMaterial(mtlfile, {CONTENTS_SOLID, CFLAGS_DETAIL}, 0.5, 0.5, 0.5);

    int vertcount = 0;
    for (const face_t *face : faces) {
        ExportObjFace(objfile, face, &vertcount);
    }
}

void ExportObj_Brushes(const std::string &filesuffix, const std::vector<const bspbrush_t *> &brushes) { }

static void ExportObj_Nodes_r(const node_t *node, std::vector<const face_t *> *dest)
{
    if (node->is_leaf) {
        return;
    }

    for (auto &face : node->facelist) {
        dest->push_back(face.get());
    }

    ExportObj_Nodes_r(node->children[0].get(), dest);
    ExportObj_Nodes_r(node->children[1].get(), dest);
}

void ExportObj_Nodes(const std::string &filesuffix, const node_t *nodes)
{
    std::vector<const face_t *> faces;
    ExportObj_Nodes_r(nodes, &faces);
    ExportObj_Faces(filesuffix, faces);
}

static void ExportObj_Marksurfaces_r(const node_t *node, std::unordered_set<const face_t *> *dest)
{
    if (!node->is_leaf) {
        ExportObj_Marksurfaces_r(node->children[0].get(), dest);
        ExportObj_Marksurfaces_r(node->children[1].get(), dest);
        return;
    }

    for (auto &face : node->markfaces) {
        if (map.mtexinfos.at(face->texinfo).flags.is_skip)
            continue;

        // FIXME: what is the face->original list about
        dest->insert(face);
    }
}

void ExportObj_Marksurfaces(const std::string &filesuffix, const node_t *nodes)
{
    // many leafs will mark the same face, so collect them in an unordered_set to filter out duplicates
    std::unordered_set<const face_t *> faces;
    ExportObj_Marksurfaces_r(nodes, &faces);

    // copy to a vector
    std::vector<const face_t *> faces_vec;
    faces_vec.reserve(faces.size());
    for (const face_t *face : faces) {
        faces_vec.push_back(face);
    }
    ExportObj_Faces(filesuffix, faces_vec);
}
