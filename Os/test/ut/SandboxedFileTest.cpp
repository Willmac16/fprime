// ======================================================================
// \title Os/test/ut/SandboxedFileTest.cpp
// \brief Unit tests for Os::SandboxedFile
// ======================================================================
#include <gtest/gtest.h>
#include <Os/FileSystem.hpp>
#include <Os/SandboxedFile.hpp>
#include <cstdio>
#include <cstring>
#include "Fw/LanguageHelpers.hpp"

// ======================================================================
// SandboxedFile tests
// ======================================================================

class SandboxedFileTest : public ::testing::Test {
  protected:
    void SetUp() override { Os::FileSystem::createDirectory("/tmp/sandbox_test/"); }
    void TearDown() override {
        Os::FileSystem::removeFile("/tmp/sandbox_test/test_file.bin");
        Os::FileSystem::removeFile("/tmp/sandbox_test/other_file.bin");
        Os::FileSystem::removeDirectory("/tmp/sandbox_test/");
    }
};

TEST_F(SandboxedFileTest, ConfigureValid) {
    Os::SandboxedFile file;
    ASSERT_TRUE(file.isConfigured());  // default is "/"
    file.configure("/tmp/sandbox_test/");
    ASSERT_TRUE(file.isConfigured());
    ASSERT_STREQ("/tmp/sandbox_test/", file.getSandboxDirectory());
}

TEST_F(SandboxedFileTest, OpenWithinSandbox) {
    Os::SandboxedFile file;
    file.configure("/tmp/sandbox_test/");
    auto status = file.open("/tmp/sandbox_test/test_file.bin", Os::File::OPEN_CREATE);
    ASSERT_EQ(Os::File::OP_OK, status);
    ASSERT_TRUE(file.isOpen());
    file.close();
}

TEST_F(SandboxedFileTest, OpenOutsideSandboxRejected) {
    Os::SandboxedFile file;
    file.configure("/tmp/sandbox_test/");
    auto status = file.open("/tmp/outside_sandbox.bin", Os::File::OPEN_CREATE);
    ASSERT_EQ(Os::File::OUTSIDE_SANDBOX, status);
    ASSERT_FALSE(file.isOpen());
}

TEST_F(SandboxedFileTest, TraversalAttackRejected) {
    Os::SandboxedFile file;
    file.configure("/tmp/sandbox_test/");
    auto status = file.open("/tmp/sandbox_test/../../etc/passwd", Os::File::OPEN_READ);
    ASSERT_EQ(Os::File::OUTSIDE_SANDBOX, status);
    ASSERT_FALSE(file.isOpen());
}

TEST_F(SandboxedFileTest, DefaultConfigAllowsAnyAbsolutePath) {
    Os::SandboxedFile file;
    // Default sandbox is "/" — any absolute path is allowed
    auto status = file.open("/tmp/sandbox_test/test_file.bin", Os::File::OPEN_CREATE);
    ASSERT_EQ(Os::File::OP_OK, status);
    ASSERT_TRUE(file.isOpen());
    file.close();
}

TEST_F(SandboxedFileTest, WriteAndRead) {
    Os::SandboxedFile file;
    file.configure("/tmp/sandbox_test/");

    // Write data
    auto status = file.open("/tmp/sandbox_test/test_file.bin", Os::File::OPEN_CREATE);
    ASSERT_EQ(Os::File::OP_OK, status);
    const U8 writeData[] = {0x01, 0x02, 0x03, 0x04};
    FwSizeType writeSize = sizeof(writeData);
    status = file.write(writeData, writeSize, Os::File::WAIT);
    ASSERT_EQ(Os::File::OP_OK, status);
    ASSERT_EQ(sizeof(writeData), writeSize);
    file.close();

    // Read data back
    status = file.open("/tmp/sandbox_test/test_file.bin", Os::File::OPEN_READ);
    ASSERT_EQ(Os::File::OP_OK, status);
    U8 readData[sizeof(writeData)];
    FwSizeType readSize = sizeof(readData);
    status = file.read(readData, readSize, Os::File::WAIT);
    ASSERT_EQ(Os::File::OP_OK, status);
    ASSERT_EQ(sizeof(writeData), readSize);
    ASSERT_EQ(0, std::memcmp(writeData, readData, sizeof(writeData)));
    file.close();
}

TEST_F(SandboxedFileTest, GetSandboxDirectoryDefault) {
    Os::SandboxedFile file;
    ASSERT_STREQ("/", file.getSandboxDirectory());
}

TEST_F(SandboxedFileTest, OpenEmptyPathRejected) {
    Os::SandboxedFile file;
    file.configure("/tmp/sandbox_test/");
    auto status = file.open("", Os::File::OPEN_READ);
    ASSERT_EQ(Os::File::OUTSIDE_SANDBOX, status);
    ASSERT_FALSE(file.isOpen());
}

TEST_F(SandboxedFileTest, OpenOverlongPathRejected) {
    Os::SandboxedFile file;
    file.configure("/tmp/sandbox_test/");
    // Create a path that exceeds MAX_PATH_LENGTH
    char longPath[Os::FilePathUtils::MAX_PATH_LENGTH + 100];
    std::memset(longPath, 'a', sizeof(longPath) - 1);
    longPath[0] = '/';
    longPath[sizeof(longPath) - 1] = '\0';
    auto status = file.open(longPath, Os::File::OPEN_READ);
    ASSERT_EQ(Os::File::OUTSIDE_SANDBOX, status);
    ASSERT_FALSE(file.isOpen());
}

TEST_F(SandboxedFileTest, MoveConstructionCarriesFileAndSandbox) {
    Os::SandboxedFile source;
    source.configure("/tmp/sandbox_test/");
    ASSERT_EQ(Os::File::OP_OK, source.open("/tmp/sandbox_test/test_file.bin", Os::File::OPEN_CREATE));

    Os::SandboxedFile destination(Fw::move(source));

    // The destination holds the open file and the sandbox it was configured with
    ASSERT_TRUE(destination.isOpen());
    ASSERT_STREQ("/tmp/sandbox_test/", destination.getSandboxDirectory());
    // Writing through the destination confirms the handle really came across
    const U8 payload[] = {1, 2, 3, 4};
    FwSizeType size = sizeof(payload);
    ASSERT_EQ(Os::File::OP_OK, destination.write(payload, size));
    ASSERT_EQ(sizeof(payload), size);

    // The source is left as if freshly constructed: closed, with the default sandbox
    ASSERT_FALSE(source.isOpen());
    ASSERT_TRUE(source.isConfigured());
    ASSERT_STREQ("/", source.getSandboxDirectory());

    destination.close();
}

TEST_F(SandboxedFileTest, MoveAssignmentClosesDestinationFile) {
    Os::SandboxedFile source;
    source.configure("/tmp/sandbox_test/");
    ASSERT_EQ(Os::File::OP_OK, source.open("/tmp/sandbox_test/test_file.bin", Os::File::OPEN_CREATE));

    Os::SandboxedFile destination;
    ASSERT_EQ(Os::File::OP_OK, destination.open("/tmp/sandbox_test/other_file.bin", Os::File::OPEN_CREATE));

    destination = Fw::move(source);

    ASSERT_TRUE(destination.isOpen());
    ASSERT_STREQ("/tmp/sandbox_test/", destination.getSandboxDirectory());
    ASSERT_FALSE(source.isOpen());
    ASSERT_STREQ("/", source.getSandboxDirectory());

    // The moved-from object is reusable, and its sandbox is back to the permissive default
    ASSERT_EQ(Os::File::OP_OK, source.open("/tmp/sandbox_test/other_file.bin", Os::File::OPEN_WRITE));
    source.close();
    destination.close();
}

TEST_F(SandboxedFileTest, SelfMoveAssignmentLeavesFileOpen) {
    Os::SandboxedFile file;
    file.configure("/tmp/sandbox_test/");
    ASSERT_EQ(Os::File::OP_OK, file.open("/tmp/sandbox_test/test_file.bin", Os::File::OPEN_CREATE));

    // Assign through an alias so this is a genuine self-move rather than a directly diagnosable one
    Os::SandboxedFile* alias = &file;
    file = Fw::move(*alias);

    ASSERT_TRUE(file.isOpen());
    ASSERT_STREQ("/tmp/sandbox_test/", file.getSandboxDirectory());
    file.close();
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
