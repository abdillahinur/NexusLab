// SPDX-FileCopyrightText: 2026 NexusLab contributors
// SPDX-License-Identifier: Apache-2.0

#include "nexuslab/version.hpp"

#include <gtest/gtest.h>

#include <string_view>

TEST(VersionTest, ReportsProjectVersion) {
    EXPECT_EQ(nexuslab::version(), std::string_view{"0.1.0"});
}
