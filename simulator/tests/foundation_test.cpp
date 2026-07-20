// SPDX-FileCopyrightText: 2026 NexusLab contributors
// SPDX-License-Identifier: Apache-2.0

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <yaml-cpp/yaml.h>

#include <string_view>

namespace {

class VersionSource {
  public:
    virtual ~VersionSource() = default;
    [[nodiscard]] virtual std::string_view current() const = 0;
};

class MockVersionSource final : public VersionSource {
  public:
    MOCK_METHOD(std::string_view, current, (), (const, override));
};

TEST(FoundationTest, GoogleMockIsAvailable) {
    MockVersionSource source;
    EXPECT_CALL(source, current()).WillOnce(testing::Return(std::string_view{"0.1.0"}));

    EXPECT_EQ(source.current(), "0.1.0");
}

TEST(FoundationTest, YamlCppIsAvailable) {
    const YAML::Node document = YAML::Load("schema_version: 1\n");

    ASSERT_TRUE(document["schema_version"]);
    EXPECT_EQ(document["schema_version"].as<int>(), 1);
}

} // namespace
