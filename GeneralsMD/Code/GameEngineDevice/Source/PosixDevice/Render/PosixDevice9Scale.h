#pragma once

#include "Platform/D3D9Posix.h"
#include "SdlGpuFrame.h"

inline unsigned int Posix_Scale_Viewport_Value(unsigned int value, unsigned int source, unsigned int destination)
{
	return source ? static_cast<unsigned int>((static_cast<unsigned long long>(value) * destination + source / 2) / source) : value;
}

inline int Posix_Scale_Rectangle_Value(int value, unsigned int source, unsigned int destination)
{
	if (!source)
		return value;
	const long long scaled = static_cast<long long>(value) * destination;
	return static_cast<int>((scaled + (scaled >= 0 ? source / 2 : -(long long)(source / 2))) / source);
}

class Posix_Scene_Viewport
{
public:
	Posix_Scene_Viewport(D3DVIEWPORT9 &viewport, const SdlGpuFrame *gpu, const SdlTarget &target)
		: Viewport(viewport), Original(viewport)
	{
		if (!gpu || !gpu->Is_Scene_Target(target))
			return;
		Viewport.X = Posix_Scale_Viewport_Value(Original.X, gpu->Width(), target.Width);
		Viewport.Y = Posix_Scale_Viewport_Value(Original.Y, gpu->Height(), target.Height);
		Viewport.Width = Posix_Scale_Viewport_Value(Original.Width, gpu->Width(), target.Width);
		Viewport.Height = Posix_Scale_Viewport_Value(Original.Height, gpu->Height(), target.Height);
	}

	~Posix_Scene_Viewport()
	{
		Viewport = Original;
	}

private:
	D3DVIEWPORT9 &Viewport;
	D3DVIEWPORT9 Original;
};
