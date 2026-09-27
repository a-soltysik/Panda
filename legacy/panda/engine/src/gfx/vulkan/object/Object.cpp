#include "panda/gfx/vulkan/object/Object.h"

#include <assimp/color4.h>
#include <assimp/material.h>
#include <assimp/mesh.h>
#include <assimp/postprocess.h>
#include <assimp/scene.h>
#include <assimp/types.h>
#include <fmt/format.h>

#include <assimp/Importer.hpp>
#include <bit>
#include <cstdint>
#include <filesystem>
#include <glm/ext/scalar_constants.hpp>
#include <glm/ext/vector_float3.hpp>
#include <glm/gtx/rotate_vector.hpp>
#include <memory>
#include <span>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "panda/gfx/vulkan/Context.h"
#include "panda/gfx/vulkan/Scene.h"
#include "panda/gfx/vulkan/Vertex.h"
#include "panda/gfx/vulkan/object/Mesh.h"
#include "panda/gfx/vulkan/object/Surface.h"
#include "panda/gfx/vulkan/object/Texture.h"
#include "panda/utils/Assert.h"

namespace panda::gfx::vulkan
{

namespace
{
auto getIndices(const aiMesh& mesh, std::vector<uint32_t>& indices) -> void
{
    const auto faces = std::span(mesh.mFaces, mesh.mNumFaces);
    for (const auto& face : faces)
    {
        const auto faceIndices = std::span(face.mIndices, face.mNumIndices);
        indices.push_back(faceIndices[0]);
        indices.push_back(faceIndices[1]);
        indices.push_back(faceIndices[2]);
    }
}

auto getVertices(const aiMesh& mesh, std::vector<Vertex>& vertices) -> void
{
    const auto meshVertices = std::span(mesh.mVertices, mesh.mNumVertices);
    const auto meshNormals = std::span(mesh.mNormals, mesh.mNumVertices);
    const auto meshTexCoords = std::span(mesh.mTextureCoords[0], mesh.mNumVertices);
    for (auto i = uint32_t {}; i < mesh.mNumVertices; i++)
    {
        auto vertex = Vertex {};
        vertex.position = glm::rotateX(std::bit_cast<glm::vec3>(meshVertices[i]), glm::pi<float>());
        vertex.normal = glm::rotateX(std::bit_cast<glm::vec3>(meshNormals[i]), glm::pi<float>());
        if (mesh.HasTextureCoords(0))
        {
            vertex.uv = {meshTexCoords[i].x, 1.F - meshTexCoords[i].y};
        }
        vertices.push_back(vertex);
    }
}

auto getTextureFromMaterial(const Context& context, const aiMaterial& material, const std::filesystem::path& parentPath)
    -> std::unique_ptr<Texture>
{
    auto textureFile = aiString {};

    if (material.GetTexture(aiTextureType_DIFFUSE, 0, &textureFile) == aiReturn_SUCCESS)
    {
        if (auto texture = Texture::fromFile(context, parentPath / std::filesystem::path {textureFile.C_Str()}))
        {
            return texture;
        }
    }

    if (auto color = aiColor4D {}; material.Get(AI_MATKEY_COLOR_DIFFUSE, color) == aiReturn_SUCCESS)
    {
        return Texture::getDefaultTexture(context, {color.r, color.g, color.b, color.a});
    }

    return Texture::getDefaultTexture(context);
}

auto getTextureCache(Context& context, const aiScene& scene, const std::filesystem::path& parentPath)
    -> std::unordered_map<uint32_t, Texture*>
{
    auto result = std::unordered_map<uint32_t, Texture*> {};

    const auto materials = std::span {scene.mMaterials, scene.mNumMaterials};
    for (auto i = uint32_t {}; i < materials.size(); i++)
    {
        auto texture = getTextureFromMaterial(context, *materials[i], parentPath);
        result.insert({i, texture.get()});
        context.registerTexture(std::move(texture));
    }

    return result;
}

}

auto Object::getId() const noexcept -> Id
{
    return _id;
}

Object::Object(std::string name, Scene& scene)
    : _name {std::move(name)},
      _scene {scene},
      _id {currentId++}
{
}

auto Object::getName() const noexcept -> const std::string&
{
    return _name;
}

auto Object::loadSurfaces(Context& context, const std::filesystem::path& path, bool shouldBeInstanced)
    -> std::vector<Surface>
{
    const auto pathStr = path.string();
    auto importer = Assimp::Importer {};
    const auto* scene =
        importer.ReadFile(pathStr.c_str(),
                          aiProcess_Triangulate | aiProcess_GenSmoothNormals | aiProcess_JoinIdenticalVertices);

    if (!shouldNotBe(scene,
                     nullptr,
                     fmt::format("Error during opening \"{}\" file: {}", pathStr.c_str(), importer.GetErrorString())))
    {
        return {};
    }

    auto result = std::vector<Surface> {};
    const auto textureCache = getTextureCache(context, *scene, path.parent_path());

    result.reserve(scene->mNumMeshes);

    for (const auto* currentMesh : std::span(scene->mMeshes, scene->mNumMeshes))
    {
        auto vertices = std::vector<vulkan::Vertex> {};
        auto indices = std::vector<uint32_t> {};
        getIndices(*currentMesh, indices);
        getVertices(*currentMesh, vertices);

        auto mesh = std::make_unique<Mesh>(currentMesh->mName.C_Str(), context.getDevice(), vertices, indices);

        result.emplace_back(textureCache.at(currentMesh->mMaterialIndex), mesh.get(), shouldBeInstanced);

        context.registerMesh(std::move(mesh));
    }
    return result;
}

auto Object::addSurface(const Surface& surface) -> void
{
    surfaces.push_back(surface);
    _scene.addSurfaceMapping(*this, surfaces.back());
}

auto Object::getSurfaces() const noexcept -> std::vector<Surface>
{
    return surfaces;
}

}
