#pragma once

#include <set>
#include <string>

#define WATCH_PROPS(...) std::set<std::string>{__VA_ARGS__}
