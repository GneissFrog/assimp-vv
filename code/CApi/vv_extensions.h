/**
 * VertexVault C API extensions for Assimp.
 *
 * These functions provide safe, ABI-stable access to bone weights,
 * bone transforms, and node manipulation — capabilities that the
 * upstream Assimp C API (cimport.h) does not expose.
 *
 * All functions use the standard Assimp C types (aiScene, aiMesh,
 * aiBone, aiNode, etc.) and are exported alongside the core library.
 */

#ifndef VV_EXTENSIONS_H
#define VV_EXTENSIONS_H

#include <assimp/types.h>
#include <assimp/mesh.h>
#include <assimp/scene.h>
#include <assimp/cexport.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ── Mesh bone read ───────────────────────────────────────────── */

/** Get the number of bones in a mesh. */
ASSIMP_API unsigned int vvGetMeshNumBones(const C_STRUCT aiScene *scene,
                                          unsigned int meshIndex);

/** Get bone name by index. Returns empty string if out of range. */
ASSIMP_API const char* vvGetBoneName(const C_STRUCT aiScene *scene,
                                     unsigned int meshIndex,
                                     unsigned int boneIndex);

/** Get number of weights for a bone. */
ASSIMP_API unsigned int vvGetBoneNumWeights(const C_STRUCT aiScene *scene,
                                            unsigned int meshIndex,
                                            unsigned int boneIndex);

/**
 * Copy bone weights into caller-provided arrays.
 *
 * @param outVertexIds  Caller-allocated uint array of size >= numWeights
 * @param outWeights    Caller-allocated float array of size >= numWeights
 * @return Number of weights written, or 0 on error.
 */
ASSIMP_API unsigned int vvGetBoneWeights(const C_STRUCT aiScene *scene,
                                         unsigned int meshIndex,
                                         unsigned int boneIndex,
                                         unsigned int *outVertexIds,
                                         float *outWeights);

/** Get bone offset matrix (4x4, row-major as stored by Assimp). */
ASSIMP_API void vvGetBoneOffsetMatrix(const C_STRUCT aiScene *scene,
                                      unsigned int meshIndex,
                                      unsigned int boneIndex,
                                      float *outMatrix16);

/* ── Bone weight write ────────────────────────────────────────── */

/**
 * Replace the weights of an existing bone.
 *
 * Allocates a new aiVertexWeight array and frees the old one.
 * The bone's mNumWeights is updated accordingly.
 *
 * @return 0 on success, -1 on error (invalid indices).
 */
ASSIMP_API int vvSetBoneWeights(C_STRUCT aiScene *scene,
                                unsigned int meshIndex,
                                unsigned int boneIndex,
                                const unsigned int *vertexIds,
                                const float *weights,
                                unsigned int numWeights);

/* ── Node transform write ─────────────────────────────────────── */

/**
 * Set a node's local transformation matrix.
 *
 * @param nodePath  Slash-separated path from root (e.g. "Armature/pelvis/spine_01")
 * @param matrix16  4x4 row-major float matrix
 * @return 0 on success, -1 if node not found.
 */
ASSIMP_API int vvSetNodeTransform(C_STRUCT aiScene *scene,
                                  const char *nodePath,
                                  const float *matrix16);

/** Get a node's local transform. Returns 0 on success, -1 if not found. */
ASSIMP_API int vvGetNodeTransform(const C_STRUCT aiScene *scene,
                                  const char *nodePath,
                                  float *outMatrix16);

/* ── Node creation ────────────────────────────────────────────── */

/**
 * Add a new child node under the specified parent.
 *
 * @param parentPath  Slash-separated path to the parent node
 * @param name        Name for the new node
 * @param matrix16    4x4 row-major local transform (NULL for identity)
 * @return 0 on success, -1 on error.
 */
ASSIMP_API int vvAddNode(C_STRUCT aiScene *scene,
                         const char *parentPath,
                         const char *name,
                         const float *matrix16);

/* ── Bone creation ────────────────────────────────────────────── */

/* ── Node manipulation ────────────────────────────────────────── */

/**
 * Rename a node (and any bone with the same name in all meshes).
 *
 * @param nodePath  Slash-separated path to the node
 * @param newName   New name for the node
 * @return 0 on success, -1 if node not found.
 */
ASSIMP_API int vvRenameNode(C_STRUCT aiScene *scene,
                             const char *nodePath,
                             const char *newName);

/**
 * Reparent a node under a new parent.
 *
 * Removes the node from its current parent's children array and
 * adds it to the new parent's children array.  The node's local
 * transform is NOT adjusted — caller must update it if needed.
 *
 * @param nodePath       Slash-separated path to the node to move
 * @param newParentPath  Slash-separated path to the new parent
 * @return 0 on success, -1 if either node not found.
 */
ASSIMP_API int vvReparentNode(C_STRUCT aiScene *scene,
                               const char *nodePath,
                               const char *newParentPath);

/**
 * Remove a node and all its children from the scene.
 *
 * Children of the removed node can optionally be reparented to the
 * removed node's parent instead of being deleted.
 *
 * @param nodePath        Slash-separated path to the node to remove
 * @param reparentChildren  If non-zero, reparent children to the removed
 *                          node's parent instead of deleting them.
 * @return 0 on success, -1 if node not found or is root.
 */
ASSIMP_API int vvRemoveNode(C_STRUCT aiScene *scene,
                             const char *nodePath,
                             int reparentChildren);

/* ── Bone creation ────────────────────────────────────────────── */

/**
 * Add a new bone to a mesh.
 *
 * @param meshIndex       Target mesh
 * @param name            Bone name
 * @param vertexIds       Vertex indices affected
 * @param weights         Per-vertex weights
 * @param numWeights      Number of entries
 * @param offsetMatrix16  Inverse bind pose (4x4 row-major float)
 * @return Index of the new bone, or -1 on error.
 */
ASSIMP_API int vvAddBone(C_STRUCT aiScene *scene,
                         unsigned int meshIndex,
                         const char *name,
                         const unsigned int *vertexIds,
                         const float *weights,
                         unsigned int numWeights,
                         const float *offsetMatrix16);

/* ── Blend shape (morph target) creation ──────────────────────── */

/**
 * Add a morph target (animation mesh) to a mesh.
 *
 * Assimp stores morph targets as aiAnimMesh entries on an aiMesh.
 * Each aiAnimMesh has per-vertex positions (and optionally normals)
 * that represent the deformed state for that shape.
 *
 * @param meshIndex       Target mesh
 * @param name            Morph target name
 * @param positions       Full vertex position array (numVertices × 3 floats).
 *                        Must have exactly mesh->mNumVertices entries.
 * @param normals         Optional per-vertex normals (same size), or NULL.
 * @param weight          Default weight (0.0–1.0)
 * @return Index of the new anim mesh, or -1 on error.
 */
ASSIMP_API int vvAddBlendShape(C_STRUCT aiScene *scene,
                                unsigned int meshIndex,
                                const char *name,
                                const float *positions,
                                const float *normals,
                                float weight);

#ifdef __cplusplus
}
#endif

#endif /* VV_EXTENSIONS_H */
