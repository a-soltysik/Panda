#include <gtest/gtest.h>

#include <panda/Mesh.hpp>
#include <utility>

TEST(Mesh, EmptyHandleReportsNoStorage)
{
    const auto mesh = panda::Mesh {};

    EXPECT_FALSE(mesh.isValid());
    EXPECT_EQ(mesh.getVertexCount(), 0);
    EXPECT_EQ(mesh.getVertexStride(), 0);
    EXPECT_EQ(mesh.getVertexSize(), 0);
    EXPECT_EQ(mesh.getIndexCount(), 0);
    EXPECT_FALSE(mesh.getIndexType());
    EXPECT_EQ(mesh.getIndexSize(), 0);
}

TEST(Mesh, CopyAndMoveOperationsPreserveEmptyHandleSemantics)
{
    auto original = panda::Mesh {};
    auto copied = original;
    auto copyAssigned = panda::Mesh {};
    copyAssigned = original;
    auto moved = std::move(copied);
    auto moveAssigned = panda::Mesh {};
    moveAssigned = std::move(copyAssigned);

    EXPECT_FALSE(original.isValid());
    EXPECT_FALSE(moved.isValid());
    EXPECT_FALSE(moveAssigned.isValid());
    // The public handle explicitly supports observing its moved-from state.
    // NOLINTNEXTLINE(bugprone-use-after-move,clang-analyzer-cplusplus.Move,hicpp-invalid-access-moved)
    EXPECT_FALSE(copied.isValid());
    // NOLINTNEXTLINE(bugprone-use-after-move,clang-analyzer-cplusplus.Move,hicpp-invalid-access-moved)
    EXPECT_FALSE(copyAssigned.isValid());
}
