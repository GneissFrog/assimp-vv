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

/* ── Helpers ──────────────────────────────────────────────────── */

static aiBone* vv_get_bone(const aiScene *scene, unsigned int mi, unsigned int bi) {
    if (!scene || mi >= scene->mNumMeshes) return nullptr;
    const aiMesh *mesh = scene->mMeshes[mi];
    if (!mesh || bi >= mesh->mNumBones) return nullptr;
    return mesh->mBones[bi];
}

static aiNode* vv_find_node(aiNode *root, const char *path) {
    if (!root || !path || !*path) return root;

    std::string p(path);
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
