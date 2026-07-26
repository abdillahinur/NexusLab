// SPDX-FileCopyrightText: 2026 NexusLab contributors
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <iosfwd>
#include <span>
#include <string_view>

namespace nexuslab::cli {

[[nodiscard]] int run(std::span<const std::string_view> arguments, std::ostream& output,
                      std::ostream& error);

} // namespace nexuslab::cli
