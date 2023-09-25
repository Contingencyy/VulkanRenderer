#pragma once
#include "renderer/RenderTypes.h"

namespace Assets
{

	void LoadTexture(const char* filepath, const char* name);
	ResourceHandle_t GetTexture(const char* name);

	void LoadGLTF(const char* filepath, const char* name);
	ResourceHandle_t GetMesh(const char* name);

}
