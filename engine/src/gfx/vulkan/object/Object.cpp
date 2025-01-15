#include "panda/gfx/vulkan/object/Object.h"

#include <assimp/postprocess.h>
#include <assimp/scene.h>

#include <assimp/Importer.hpp>
#include <glm/ext.hpp>

#include "panda/gfx/vulkan/Context.h"

namespace panda::gfx::vulkan
{

namespace
{
auto getIndices(const aiMesh& mesh, std::vector<uint32_t>& indices, uint32_t offset = 0) -> void
{
    const auto faces = std::span(mesh.mFaces, mesh.mNumFaces);
    for (const auto& face : faces)
    {
        const auto faceIndices = std::span(face.mIndices, face.mNumIndices);
        indices.push_back(faceIndices[0] + offset);
        indices.push_back(faceIndices[1] + offset);
        indices.push_back(faceIndices[2] + offset);
    }
}

auto getVertices(const aiMesh& mesh, std::vector<panda::gfx::vulkan::Vertex>& vertices) -> void
{
    static constexpr auto defaultColor = glm::vec3(1);
    static constexpr auto defaultTexCoord = glm::vec3(0);

    const auto meshVertices = std::span(mesh.mVertices, mesh.mNumVertices);
    const auto meshNormals = std::span(mesh.mNormals, mesh.mNumVertices);
    const auto meshTexCoords = std::span(mesh.mTextureCoords[0], mesh.mNumVertices);
    for (auto i = uint32_t {}; i < mesh.mNumVertices; i++)
    {
        auto vertex = panda::gfx::vulkan::Vertex {};
        vertex.position = glm::rotateX(std::bit_cast<glm::vec3>(meshVertices[i]), glm::pi<float>());
        vertex.normal = glm::rotateX(std::bit_cast<glm::vec3>(meshNormals[i]), glm::pi<float>());
        if (mesh.HasTextureCoords(0))
        {
            vertex.uv = {meshTexCoords[i].x, 1.F - meshTexCoords[i].y};
        }
        vertices.push_back(vertex);
    }
}

auto getTextureFromMaterial(const Context& context,
                            const aiMaterial& material,
                            const std::filesystem::path& parentPath) -> std::unique_ptr<Texture>
{
    auto textureFile = aiString {};

    if (material.GetTexture(aiTextureType_DIFFUSE, 0, &textureFile) == aiReturn_SUCCESS)
    {
        auto texture = Texture::fromFile(context, parentPath / std::filesystem::path {textureFile.C_Str()});
        if (texture)
        {
            return texture;
        }
    }

    auto color = aiColor4D {};
    if (material.Get(AI_MATKEY_COLOR_DIFFUSE, color) == aiReturn_SUCCESS)
    {
        return Texture::getDefaultTexture(context, {color.r, color.g, color.b, color.a});
    }

    return Texture::getDefaultTexture(context);
}

auto getTextureCache(Context& context,
                     const aiScene& scene,
                     const std::filesystem::path& parentPath) -> std::unordered_map<uint32_t, Texture*>
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

Object::Object(std::string name)
    : _name {std::move(name)},
      _id {currentId++}
{
}

auto Object::getName() const noexcept -> const std::string&
{
    return _name;
}

auto Transform::mat4() const noexcept -> glm::mat4
{
    const float c3 = glm::cos(rotation.z);
    const float s3 = glm::sin(rotation.z);
    const float c2 = glm::cos(rotation.x);
    const float s2 = glm::sin(rotation.x);
    const float c1 = glm::cos(rotation.y);
    const float s1 = glm::sin(rotation.y);
    return glm::mat4 {
        {
         scale.x * (c1 * c3 + s1 * s2 * s3),
         scale.x * (c2 * s3),
         scale.x * (c1 * s2 * s3 - c3 * s1),
         0.0F, },
        {
         scale.y * (c3 * s1 * s2 - c1 * s3),
         scale.y * (c2 * c3),
         scale.y * (c1 * c3 * s2 + s1 * s3),
         0.0F, },
        {
         scale.z * (c2 * s1),
         scale.z * (-s2),
         scale.z * (c1 * c2),
         0.0F, },
        {translation.x, translation.y, translation.z, 1.0F}
    };
}

auto Transform::normalMatrix() const noexcept -> glm::mat3
{
    const float c3 = glm::cos(rotation.z);
    const float s3 = glm::sin(rotation.z);
    const float c2 = glm::cos(rotation.x);
    const float s2 = glm::sin(rotation.x);
    const float c1 = glm::cos(rotation.y);
    const float s1 = glm::sin(rotation.y);
    const auto invScale = 1.F / scale;
    return glm::mat3 {
        {invScale.x * (c1 * c3 + s1 * s2 * s3), invScale.x * (c2 * s3), invScale.x * (c1 * s2 * s3 - c3 * s1)},
        {invScale.y * (c3 * s1 * s2 - c1 * s3), invScale.y * (c2 * c3), invScale.y * (c1 * c3 * s2 + s1 * s3)},
        {invScale.z * (c2 * s1),                invScale.z * (-s2),     invScale.z * (c1 * c2)               },
    };
}

auto Object::loadSurfaces(Context& context, const std::filesystem::path& path) -> std::vector<Surface>
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
    auto textureCache = getTextureCache(context, *scene, path.parent_path());

    result.reserve(scene->mNumMeshes);

    const auto meshes = std::span(scene->mMeshes, scene->mNumMeshes);
    for (const auto* currentMesh : meshes)
    {
        auto vertices = std::vector<vulkan::Vertex> {};
        auto indices = std::vector<uint32_t> {};
        getIndices(*currentMesh, indices);
        getVertices(*currentMesh, vertices);

        auto mesh = std::make_unique<Mesh>(currentMesh->mName.C_Str(), context.getDevice(), vertices, indices);

        result.push_back({textureCache.at(currentMesh->mMaterialIndex), mesh.get()});

        context.registerMesh(std::move(mesh));
    }
    return result;
}

}
