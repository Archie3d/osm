#pragma once

#include <string>
#include <expected>

namespace osm {

using Error = std::string;

template <typename T>
using Result = std::expected<T, Error>;

} // namespace osm
