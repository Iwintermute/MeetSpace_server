#pragma once

#define IMGUI_DEFINE_MATH_OPERATORS

#include <algorithm>
#include <vector>
#include <sstream>
#include <string>
#include <memory>
#include <map>
#include<unordered_map>
#include <variant>
#include <array>

#include "imgui.h"
#include "imgui_internal.h"
#include "imgui_freetype.h"
#include "backends/imgui_impl_dx11.h"

#include "config.h"
#include "flags.h"
#include "search.h"
#include "../settings/colors.h"
#include "../settings/elements.h"
#include "../settings/variables.h"
#include "fonts.h"
#include "draw.h"
#include "functions.h"
#include "../data/fonts.h"
#include "blur.h"
