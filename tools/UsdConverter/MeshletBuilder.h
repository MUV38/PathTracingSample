#pragma once

#include "SceneFile.h"

// Input: triangle-list vertices. Output: deduplicated vertices and spatial meshlets.
void buildSceneMeshlets(SceneData& scene);
