#pragma once
#include <glm/glm.hpp>

struct LightingUBO {
    glm::vec4 sunDir;    // xyz = normalized sun direction (towards scene), w = unused
    glm::vec4 sunColor;  // rgb = sun color/intensity, a = unused
    glm::vec4 ambient;   // rgb = ambient color, a = unused
};