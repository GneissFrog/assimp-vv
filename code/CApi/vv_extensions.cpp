/**
 * VertexVault C API extensions for Assimp — implementation.
 */

#include "vv_extensions.h"
#include <assimp/scene.h>
#include <assimp/mesh.h>
#include <cstring>
#include <cstdlib>
#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>

/* ── Helpers ──────────────────────────────────────────────────── */

static aiBone* vv_get_bone(const aiScene *scene, unsigned int mi, unsigned int bi) {
    if (!scene || mi >= scene->mNumMeshes) return nullptr;
    const aiMesh *mesh = scene->mMeshes[mi];
    if (!mesh || bi >= mesh->mNumBones) return nullptr;
    return mesh->mBones[bi];
}

static aiNode* vv_find_node_recursive(aiNode *node, const std::string &name) {
    if (!node) return nullptr;
    if (std::string(node->mName.C_Str()) == name) return node;
    for (unsigned int i = 0; i < node->mNumChildren; ++i) {
        aiNode *found = vv_find_node_recursive(node->mChildren[i], name);
        if (found) return found;
    }
    return nullptr;
}

static aiNode* vv_find_node(aiNode *root, const char *path) {
    if (!root || !path || !*path) return root;

    std::string p(path);

    /* Bare name (no slashes) — recursive DFS through entire tree */
    if (p.find('/') == std::string::npos) {
        return vv_find_node_recursive(root, p);
    }

    /* Slash-separated path — walk from root one segment at a time */
    aiNode *cur = root;
    size_t pos = 0;

    while (pos < p.size() && cur) {
        size_t slash = p.find('/', pos);
        if (slash == std::string::npos) slash = p.size();
        std::string seg = p.substr(pos, slash - pos);
        pos = slash + 1;

        if (seg.empty()) continue;

        aiNode *found = nullptr;
        for (unsigned int i = 0; i < cur->mNumChildren; ++i) {
            if (cur->mChildren[i]->mName.C_Str() == seg ||
                std::string(cur->mChildren[i]->mName.C_Str()) == seg) {
                found = cur->mChildren[i];
                break;
            }
        }
        cur = found;
    }
    return cur;
}

/* ── Mesh bone read ───────────────────────────────────────────── */

unsigned int vvGetMeshNumBones(const aiScene *scene, unsigned int mi) {
    if (!scene || mi >= scene->mNumMeshes) return 0;
    return scene->mMeshes[mi]->mNumBones;
}

const char* vvGetBoneName(const aiScene *scene, unsigned int mi, unsigned int bi) {
    const aiBone *bone = vv_get_bone(scene, mi, bi);
    return bone ? bone->mName.C_Str() : "";
}

unsigned int vvGetBoneNumWeights(const aiScene *scene, unsigned int mi, unsigned int bi) {
    const aiBone *bone = vv_get_bone(scene, mi, bi);
    return bone ? bone->mNumWeights : 0;
}

unsigned int vvGetBoneWeights(const aiScene *scene, unsigned int mi, unsigned int bi,
                              unsigned int *outVids, float *outWts) {
    const aiBone *bone = vv_get_bone(scene, mi, bi);
    if (!bone || !outVids || !outWts) return 0;

    for (unsigned int i = 0; i < bone->mNumWeights; ++i) {
        outVids[i] = bone->mWeights[i].mVertexId;
        outWts[i]  = bone->mWeights[i].mWeight;
    }
    return bone->mNumWeights;
}

void vvGetBoneOffsetMatrix(const aiScene *scene, unsigned int mi, unsigned int bi,
                           float *out16) {
    const aiBone *bone = vv_get_bone(scene, mi, bi);
    if (!bone || !out16) return;
    std::memcpy(out16, &bone->mOffsetMatrix, 16 * sizeof(float));
}

int vvSetBoneOffsetMatrix(aiScene *scene, unsigned int mi, unsigned int bi,
                          const float *in16) {
    aiBone *bone = vv_get_bone(scene, mi, bi);
    if (!bone || !in16) return -1;
    std::memcpy(&bone->mOffsetMatrix, in16, 16 * sizeof(float));
    return 0;
}

/* ── Bone weight write ────────────────────────────────────────── */

int vvSetBoneWeights(aiScene *scene, unsigned int mi, unsigned int bi,
                     const unsigned int *vids, const float *wts, unsigned int nw) {
    aiBone *bone = vv_get_bone(scene, mi, bi);
    if (!bone) return -1;

    // Free old weights
    delete[] bone->mWeights;

    // Allocate and copy new weights
    bone->mNumWeights = nw;
    if (nw > 0) {
        bone->mWeights = new aiVertexWeight[nw];
        for (unsigned int i = 0; i < nw; ++i) {
            bone->mWeights[i].mVertexId = vids[i];
            bone->mWeights[i].mWeight   = wts[i];
        }
    } else {
        bone->mWeights = nullptr;
    }
    return 0;
}

/* ── Node transform ───────────────────────────────────────────── */

int vvSetNodeTransform(aiScene *scene, const char *path, const float *m16) {
    if (!scene || !path || !m16) return -1;
    aiNode *node = vv_find_node(scene->mRootNode, path);
    if (!node) return -1;
    std::memcpy(&node->mTransformation, m16, 16 * sizeof(float));
    return 0;
}

int vvGetNodeTransform(const aiScene *scene, const char *path, float *out16) {
    if (!scene || !path || !out16) return -1;
    const aiNode *node = vv_find_node(scene->mRootNode, path);
    if (!node) return -1;
    std::memcpy(out16, &node->mTransformation, 16 * sizeof(float));
    return 0;
}

/* ── Node manipulation ────────────────────────────────────────── */

int vvRenameNode(aiScene *scene, const char *path, const char *newName) {
    if (!scene || !path || !newName || !*newName) return -1;
    aiNode *node = vv_find_node(scene->mRootNode, path);
    if (!node) return -1;

    std::string oldName(node->mName.C_Str());
    node->mName = aiString(std::string(newName));

    // Rename matching bones in all meshes (bone names must match node names)
    for (unsigned int mi = 0; mi < scene->mNumMeshes; ++mi) {
        aiMesh *mesh = scene->mMeshes[mi];
        if (!mesh) continue;
        for (unsigned int bi = 0; bi < mesh->mNumBones; ++bi) {
            if (std::string(mesh->mBones[bi]->mName.C_Str()) == oldName) {
                mesh->mBones[bi]->mName = aiString(std::string(newName));
            }
        }
    }
    return 0;
}

static void vv_remove_child(aiNode *parent, aiNode *child) {
    if (!parent || !child || parent->mNumChildren == 0) return;

    unsigned int newCount = parent->mNumChildren - 1;
    if (newCount == 0) {
        delete[] parent->mChildren;
        parent->mChildren = nullptr;
        parent->mNumChildren = 0;
        return;
    }

    aiNode **newArr = new aiNode*[newCount];
    unsigned int j = 0;
    for (unsigned int i = 0; i < parent->mNumChildren; ++i) {
        if (parent->mChildren[i] != child) {
            if (j < newCount) newArr[j++] = parent->mChildren[i];
        }
    }
    delete[] parent->mChildren;
    parent->mChildren = newArr;
    parent->mNumChildren = j;
}

static void vv_add_child(aiNode *parent, aiNode *child) {
    unsigned int newCount = parent->mNumChildren + 1;
    aiNode **newArr = new aiNode*[newCount];
    if (parent->mChildren && parent->mNumChildren > 0) {
        std::memcpy(newArr, parent->mChildren, parent->mNumChildren * sizeof(aiNode*));
        delete[] parent->mChildren;
    }
    newArr[newCount - 1] = child;
    parent->mChildren = newArr;
    parent->mNumChildren = newCount;
    child->mParent = parent;
}

int vvReparentNode(aiScene *scene, const char *nodePath, const char *newParentPath) {
    if (!scene || !nodePath || !newParentPath) return -1;
    aiNode *node = vv_find_node(scene->mRootNode, nodePath);
    aiNode *newParent = vv_find_node(scene->mRootNode, newParentPath);
    if (!node || !newParent) return -1;
    if (node == scene->mRootNode) return -1;  // Can't reparent root
    if (node == newParent) return -1;          // Can't parent to self

    // Remove from current parent
    aiNode *oldParent = node->mParent;
    if (oldParent) {
        vv_remove_child(oldParent, node);
    }

    // Add to new parent
    vv_add_child(newParent, node);
    return 0;
}

static void vv_delete_node_recursive(aiNode *node) {
    if (!node) return;
    for (unsigned int i = 0; i < node->mNumChildren; ++i) {
        vv_delete_node_recursive(node->mChildren[i]);
    }
    delete[] node->mChildren;
    node->mChildren = nullptr;
    node->mNumChildren = 0;
    delete node;
}

int vvRemoveNode(aiScene *scene, const char *path, int reparentChildren) {
    if (!scene || !path) return -1;
    aiNode *node = vv_find_node(scene->mRootNode, path);
    if (!node || node == scene->mRootNode) return -1;

    aiNode *parent = node->mParent;
    if (!parent) return -1;

    if (reparentChildren) {
        // Move children to the removed node's parent
        for (unsigned int i = 0; i < node->mNumChildren; ++i) {
            vv_add_child(parent, node->mChildren[i]);
        }
        // Clear children array (don't delete them — they were reparented)
        delete[] node->mChildren;
        node->mChildren = nullptr;
        node->mNumChildren = 0;
    } else {
        // Delete all children recursively
        for (unsigned int i = 0; i < node->mNumChildren; ++i) {
            vv_delete_node_recursive(node->mChildren[i]);
        }
        delete[] node->mChildren;
        node->mChildren = nullptr;
        node->mNumChildren = 0;
    }

    // Remove node from parent
    vv_remove_child(parent, node);
    delete node;
    return 0;
}

/* ── Node creation ────────────────────────────────────────────── */

int vvAddNode(aiScene *scene, const char *parentPath, const char *name,
              const float *m16) {
    if (!scene || !name || !*name) return -1;
    aiNode *parent = vv_find_node(scene->mRootNode, parentPath ? parentPath : "");
    if (!parent) return -1;

    // Create new node
    aiNode *child = new aiNode(std::string(name));
    child->mParent = parent;

    if (m16) {
        std::memcpy(&child->mTransformation, m16, 16 * sizeof(float));
    }
    // else: default constructor sets identity

    // Expand parent's children array
    unsigned int newCount = parent->mNumChildren + 1;
    aiNode **newArr = new aiNode*[newCount];
    if (parent->mChildren && parent->mNumChildren > 0) {
        std::memcpy(newArr, parent->mChildren, parent->mNumChildren * sizeof(aiNode*));
        delete[] parent->mChildren;
    }
    newArr[newCount - 1] = child;
    parent->mChildren = newArr;
    parent->mNumChildren = newCount;

    return 0;
}

/* ── Bone creation ────────────────────────────────────────────── */

int vvAddBone(aiScene *scene, unsigned int mi, const char *name,
              const unsigned int *vids, const float *wts, unsigned int nw,
              const float *offset16) {
    if (!scene || mi >= scene->mNumMeshes || !name) return -1;
    aiMesh *mesh = scene->mMeshes[mi];
    if (!mesh) return -1;

    // Create new bone
    aiBone *bone = new aiBone();
    bone->mName = aiString(std::string(name));
    bone->mNumWeights = nw;

    if (nw > 0 && vids && wts) {
        bone->mWeights = new aiVertexWeight[nw];
        for (unsigned int i = 0; i < nw; ++i) {
            bone->mWeights[i].mVertexId = vids[i];
            bone->mWeights[i].mWeight   = wts[i];
        }
    }

    if (offset16) {
        std::memcpy(&bone->mOffsetMatrix, offset16, 16 * sizeof(float));
    }

    // Expand mesh's bones array
    unsigned int newCount = mesh->mNumBones + 1;
    aiBone **newArr = new aiBone*[newCount];
    if (mesh->mBones && mesh->mNumBones > 0) {
        std::memcpy(newArr, mesh->mBones, mesh->mNumBones * sizeof(aiBone*));
        delete[] mesh->mBones;
    }
    newArr[newCount - 1] = bone;
    mesh->mBones = newArr;
    mesh->mNumBones = newCount;

    return (int)(newCount - 1);
}

/* ── Blend shape (morph target) creation ─────────────────────── */

int vvAddBlendShape(aiScene *scene, unsigned int mi, const char *name,
                     const float *positions, const float *normals, float weight) {
    if (!scene || mi >= scene->mNumMeshes || !name || !positions) return -1;
    aiMesh *mesh = scene->mMeshes[mi];
    if (!mesh || mesh->mNumVertices == 0) return -1;

    unsigned int nv = mesh->mNumVertices;

    // Create new aiAnimMesh
    aiAnimMesh *animMesh = new aiAnimMesh();
    animMesh->mName = aiString(std::string(name));
    animMesh->mNumVertices = nv;
    animMesh->mWeight = weight;

    // Copy positions
    animMesh->mVertices = new aiVector3D[nv];
    for (unsigned int i = 0; i < nv; ++i) {
        animMesh->mVertices[i].x = positions[i * 3 + 0];
        animMesh->mVertices[i].y = positions[i * 3 + 1];
        animMesh->mVertices[i].z = positions[i * 3 + 2];
    }

    // Copy normals if provided
    if (normals) {
        animMesh->mNormals = new aiVector3D[nv];
        for (unsigned int i = 0; i < nv; ++i) {
            animMesh->mNormals[i].x = normals[i * 3 + 0];
            animMesh->mNormals[i].y = normals[i * 3 + 1];
            animMesh->mNormals[i].z = normals[i * 3 + 2];
        }
    }

    // Expand mesh's anim meshes array
    unsigned int newCount = mesh->mNumAnimMeshes + 1;
    aiAnimMesh **newArr = new aiAnimMesh*[newCount];
    if (mesh->mAnimMeshes && mesh->mNumAnimMeshes > 0) {
        std::memcpy(newArr, mesh->mAnimMeshes, mesh->mNumAnimMeshes * sizeof(aiAnimMesh*));
        delete[] mesh->mAnimMeshes;
    }
    newArr[newCount - 1] = animMesh;
    mesh->mAnimMeshes = newArr;
    mesh->mNumAnimMeshes = newCount;

    return (int)(newCount - 1);
}

/* ── Mesh data read ──────────────────────────────────────────── */

unsigned int vvGetMeshNumVertices(const aiScene *scene, unsigned int mi) {
    if (!scene || mi >= scene->mNumMeshes) return 0;
    return scene->mMeshes[mi]->mNumVertices;
}

unsigned int vvGetMeshNumFaces(const aiScene *scene, unsigned int mi) {
    if (!scene || mi >= scene->mNumMeshes) return 0;
    return scene->mMeshes[mi]->mNumFaces;
}

unsigned int vvGetMeshVertices(const aiScene *scene, unsigned int mi, float *out) {
    if (!scene || mi >= scene->mNumMeshes || !out) return 0;
    const aiMesh *mesh = scene->mMeshes[mi];
    if (!mesh || !mesh->mVertices) return 0;
    for (unsigned int i = 0; i < mesh->mNumVertices; ++i) {
        out[i * 3 + 0] = mesh->mVertices[i].x;
        out[i * 3 + 1] = mesh->mVertices[i].y;
        out[i * 3 + 2] = mesh->mVertices[i].z;
    }
    return mesh->mNumVertices;
}

int vvGetMeshFaces(const aiScene *scene, unsigned int mi,
                   unsigned int *outIdx, unsigned int *outCount) {
    if (!scene || mi >= scene->mNumMeshes || !outIdx || !outCount) return -1;
    const aiMesh *mesh = scene->mMeshes[mi];
    if (!mesh || !mesh->mFaces) return -1;
    unsigned int written = 0;
    for (unsigned int fi = 0; fi < mesh->mNumFaces; ++fi) {
        const aiFace &f = mesh->mFaces[fi];
        if (f.mNumIndices != 3) continue;  // skip non-triangles
        outIdx[written * 3 + 0] = f.mIndices[0];
        outIdx[written * 3 + 1] = f.mIndices[1];
        outIdx[written * 3 + 2] = f.mIndices[2];
        ++written;
    }
    *outCount = written;
    return 0;
}

/* ── Mesh subset extraction ──────────────────────────────────── */

int vvExtractMeshSubset(aiScene *scene, unsigned int mi,
                        const unsigned int *keepVids, unsigned int numKeep) {
    if (!scene || mi >= scene->mNumMeshes || !keepVids || numKeep == 0)
        return -1;

    aiMesh *mesh = scene->mMeshes[mi];
    if (!mesh || !mesh->mVertices || !mesh->mFaces || mesh->mNumVertices == 0)
        return -1;

    // 1. Build keep set, clamping to valid range
    std::unordered_set<unsigned int> keepSet;
    keepSet.reserve(numKeep);
    for (unsigned int i = 0; i < numKeep; ++i) {
        if (keepVids[i] < mesh->mNumVertices)
            keepSet.insert(keepVids[i]);
    }
    if (keepSet.empty()) return -1;

    // 2. Find faces where ALL vertices are in the keep set
    std::vector<unsigned int> keptFaceIdx;
    keptFaceIdx.reserve(mesh->mNumFaces);
    for (unsigned int fi = 0; fi < mesh->mNumFaces; ++fi) {
        const aiFace &face = mesh->mFaces[fi];
        if (face.mNumIndices == 0 || !face.mIndices) continue;
        bool allIn = true;
        for (unsigned int j = 0; j < face.mNumIndices; ++j) {
            if (keepSet.find(face.mIndices[j]) == keepSet.end()) {
                allIn = false;
                break;
            }
        }
        if (allIn) keptFaceIdx.push_back(fi);
    }

    if (keptFaceIdx.empty()) return -1;

    // 3. Collect actual used vertices from kept faces (sorted)
    std::unordered_set<unsigned int> usedSet;
    for (unsigned int fi : keptFaceIdx) {
        const aiFace &face = mesh->mFaces[fi];
        for (unsigned int j = 0; j < face.mNumIndices; ++j)
            usedSet.insert(face.mIndices[j]);
    }
    std::vector<unsigned int> usedVerts(usedSet.begin(), usedSet.end());
    std::sort(usedVerts.begin(), usedVerts.end());

    // 4. Build old -> new vertex remap
    unsigned int newNV = (unsigned int)usedVerts.size();
    std::unordered_map<unsigned int, unsigned int> remap;
    remap.reserve(newNV);
    for (unsigned int i = 0; i < newNV; ++i)
        remap[usedVerts[i]] = i;

    // 5. Rebuild per-vertex arrays
    // NOTE: We intentionally do NOT free the old arrays.  Assimp's
    // scene destructor (aiReleaseImport) will clean up the entire
    // allocation block.  Calling delete[] on Assimp-allocated memory
    // crashes because it may use a different allocator.

    // Positions
    aiVector3D *newPos = new aiVector3D[newNV];
    for (unsigned int i = 0; i < newNV; ++i)
        newPos[i] = mesh->mVertices[usedVerts[i]];
    mesh->mVertices = newPos;

    // Normals
    if (mesh->mNormals) {
        aiVector3D *buf = new aiVector3D[newNV];
        for (unsigned int i = 0; i < newNV; ++i)
            buf[i] = mesh->mNormals[usedVerts[i]];
        mesh->mNormals = buf;
    }

    // Tangents
    if (mesh->mTangents) {
        aiVector3D *buf = new aiVector3D[newNV];
        for (unsigned int i = 0; i < newNV; ++i)
            buf[i] = mesh->mTangents[usedVerts[i]];
        mesh->mTangents = buf;
    }

    // Bitangents
    if (mesh->mBitangents) {
        aiVector3D *buf = new aiVector3D[newNV];
        for (unsigned int i = 0; i < newNV; ++i)
            buf[i] = mesh->mBitangents[usedVerts[i]];
        mesh->mBitangents = buf;
    }

    // Texture coordinates (all channels)
    for (int ch = 0; ch < AI_MAX_NUMBER_OF_TEXTURECOORDS; ++ch) {
        if (mesh->mTextureCoords[ch]) {
            aiVector3D *buf = new aiVector3D[newNV];
            for (unsigned int i = 0; i < newNV; ++i)
                buf[i] = mesh->mTextureCoords[ch][usedVerts[i]];
            mesh->mTextureCoords[ch] = buf;
        }
    }

    // Vertex colors (all channels)
    for (int ch = 0; ch < AI_MAX_NUMBER_OF_COLOR_SETS; ++ch) {
        if (mesh->mColors[ch]) {
            aiColor4D *buf = new aiColor4D[newNV];
            for (unsigned int i = 0; i < newNV; ++i)
                buf[i] = mesh->mColors[ch][usedVerts[i]];
            mesh->mColors[ch] = buf;
        }
    }

    // 6. Rebuild faces with remapped indices
    unsigned int newNF = (unsigned int)keptFaceIdx.size();
    aiFace *newFaces = new aiFace[newNF];
    for (unsigned int i = 0; i < newNF; ++i) {
        const aiFace &old = mesh->mFaces[keptFaceIdx[i]];
        newFaces[i].mNumIndices = old.mNumIndices;
        newFaces[i].mIndices = new unsigned int[old.mNumIndices];
        for (unsigned int j = 0; j < old.mNumIndices; ++j)
            newFaces[i].mIndices[j] = remap[old.mIndices[j]];
    }
    mesh->mFaces = newFaces;

    // 7. Remap bone weights
    for (unsigned int bi = 0; bi < mesh->mNumBones; ++bi) {
        aiBone *bone = mesh->mBones[bi];
        std::vector<aiVertexWeight> kept;
        kept.reserve(bone->mNumWeights);
        for (unsigned int wi = 0; wi < bone->mNumWeights; ++wi) {
            auto it = remap.find(bone->mWeights[wi].mVertexId);
            if (it != remap.end()) {
                aiVertexWeight w;
                w.mVertexId = it->second;
                w.mWeight = bone->mWeights[wi].mWeight;
                kept.push_back(w);
            }
        }
        delete[] bone->mWeights;
        bone->mNumWeights = (unsigned int)kept.size();
        if (!kept.empty()) {
            bone->mWeights = new aiVertexWeight[kept.size()];
            std::memcpy(bone->mWeights, kept.data(),
                        kept.size() * sizeof(aiVertexWeight));
        } else {
            bone->mWeights = nullptr;
        }
    }

    // 8. Update counts
    mesh->mNumVertices = newNV;
    mesh->mNumFaces = newNF;

    return 0;
}

/* ── vvReplaceMeshData ────────────────────────────────────────── */

int vvReplaceMeshData(aiScene *scene,
                      unsigned int meshIndex,
                      const float *positions,
                      const float *uvs,
                      unsigned int numNewVerts,
                      const unsigned int *indices,
                      unsigned int numNewFaces,
                      const unsigned int *vmapping) {
    if (!scene || !positions || !uvs || !indices || !vmapping)
        return -1;
    unsigned int nm = scene->mNumMeshes;
    if (meshIndex >= nm)
        return -1;

    aiMesh *mesh = scene->mMeshes[meshIndex];
    if (!mesh)
        return -1;

    // 1. Replace vertex positions
    aiVector3D *newPos = new aiVector3D[numNewVerts];
    for (unsigned int i = 0; i < numNewVerts; ++i) {
        newPos[i].x = positions[i * 3];
        newPos[i].y = positions[i * 3 + 1];
        newPos[i].z = positions[i * 3 + 2];
    }
    mesh->mVertices = newPos;

    // 2. Compute normals from the new faces
    aiVector3D *newNormals = new aiVector3D[numNewVerts];
    std::memset(newNormals, 0, numNewVerts * sizeof(aiVector3D));
    for (unsigned int fi = 0; fi < numNewFaces; ++fi) {
        unsigned int i0 = indices[fi * 3];
        unsigned int i1 = indices[fi * 3 + 1];
        unsigned int i2 = indices[fi * 3 + 2];
        if (i0 >= numNewVerts || i1 >= numNewVerts || i2 >= numNewVerts)
            continue;
        aiVector3D e1 = newPos[i1] - newPos[i0];
        aiVector3D e2 = newPos[i2] - newPos[i0];
        aiVector3D n(e1.y * e2.z - e1.z * e2.y,
                     e1.z * e2.x - e1.x * e2.z,
                     e1.x * e2.y - e1.y * e2.x);
        newNormals[i0] = newNormals[i0] + n;
        newNormals[i1] = newNormals[i1] + n;
        newNormals[i2] = newNormals[i2] + n;
    }
    for (unsigned int i = 0; i < numNewVerts; ++i) {
        float len = std::sqrt(newNormals[i].x * newNormals[i].x +
                              newNormals[i].y * newNormals[i].y +
                              newNormals[i].z * newNormals[i].z);
        if (len > 1e-10f) {
            newNormals[i].x /= len;
            newNormals[i].y /= len;
            newNormals[i].z /= len;
        }
    }
    mesh->mNormals = newNormals;

    // 3. Clear tangents/bitangents (will be recomputed by loader if needed)
    mesh->mTangents = nullptr;
    mesh->mBitangents = nullptr;

    // 4. Replace UV coordinates (channel 0)
    aiVector3D *newUVs = new aiVector3D[numNewVerts];
    for (unsigned int i = 0; i < numNewVerts; ++i) {
        newUVs[i].x = uvs[i * 2];
        newUVs[i].y = uvs[i * 2 + 1];
        newUVs[i].z = 0.0f;
    }
    mesh->mTextureCoords[0] = newUVs;
    mesh->mNumUVComponents[0] = 2;
    // Clear other UV channels
    for (int ch = 1; ch < AI_MAX_NUMBER_OF_TEXTURECOORDS; ++ch) {
        mesh->mTextureCoords[ch] = nullptr;
        mesh->mNumUVComponents[ch] = 0;
    }

    // 5. Clear vertex colors
    for (int ch = 0; ch < AI_MAX_NUMBER_OF_COLOR_SETS; ++ch)
        mesh->mColors[ch] = nullptr;

    // 6. Replace faces
    aiFace *newFaces = new aiFace[numNewFaces];
    for (unsigned int fi = 0; fi < numNewFaces; ++fi) {
        newFaces[fi].mNumIndices = 3;
        newFaces[fi].mIndices = new unsigned int[3];
        newFaces[fi].mIndices[0] = indices[fi * 3];
        newFaces[fi].mIndices[1] = indices[fi * 3 + 1];
        newFaces[fi].mIndices[2] = indices[fi * 3 + 2];
    }
    mesh->mFaces = newFaces;

    // 7. Remap bone weights through vmapping
    // For each bone: new vertex i inherits the weight of original vertex vmapping[i]
    for (unsigned int bi = 0; bi < mesh->mNumBones; ++bi) {
        aiBone *bone = mesh->mBones[bi];

        // Build a lookup from original vertex ID → weight
        std::unordered_map<unsigned int, float> origWeights;
        origWeights.reserve(bone->mNumWeights);
        for (unsigned int wi = 0; wi < bone->mNumWeights; ++wi)
            origWeights[bone->mWeights[wi].mVertexId] = bone->mWeights[wi].mWeight;

        // Map through vmapping
        std::vector<aiVertexWeight> remapped;
        remapped.reserve(bone->mNumWeights * 2);  // may grow due to split verts
        for (unsigned int newVI = 0; newVI < numNewVerts; ++newVI) {
            unsigned int origVI = vmapping[newVI];
            auto it = origWeights.find(origVI);
            if (it != origWeights.end()) {
                aiVertexWeight w;
                w.mVertexId = newVI;
                w.mWeight = it->second;
                remapped.push_back(w);
            }
        }

        delete[] bone->mWeights;
        bone->mNumWeights = (unsigned int)remapped.size();
        if (!remapped.empty()) {
            bone->mWeights = new aiVertexWeight[remapped.size()];
            std::memcpy(bone->mWeights, remapped.data(),
                        remapped.size() * sizeof(aiVertexWeight));
        } else {
            bone->mWeights = nullptr;
        }
    }

    // 8. Update counts
    mesh->mNumVertices = numNewVerts;
    mesh->mNumFaces = numNewFaces;

    return 0;
}
