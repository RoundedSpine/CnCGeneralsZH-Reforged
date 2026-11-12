#pragma once

namespace RenderResolution
{
	enum Scale
	{
		NATIVE = 100,
		HIGH = 75,
		BALANCED = 67,
		PERFORMANCE = 50
	};

	inline bool Dimensions(unsigned int width, unsigned int height, unsigned int scale,
		unsigned int *scaledWidth, unsigned int *scaledHeight)
	{
		if (!width || !height || !scaledWidth || !scaledHeight
			|| (scale != NATIVE && scale != HIGH && scale != BALANCED && scale != PERFORMANCE))
			return false;

		if (scale == BALANCED)
		{
			*scaledWidth = static_cast<unsigned int>((static_cast<unsigned long long>(width) * 2 + 1) / 3);
			*scaledHeight = static_cast<unsigned int>((static_cast<unsigned long long>(height) * 2 + 1) / 3);
		}
		else
		{
			*scaledWidth = static_cast<unsigned int>((static_cast<unsigned long long>(width) * scale + 50) / 100);
			*scaledHeight = static_cast<unsigned int>((static_cast<unsigned long long>(height) * scale + 50) / 100);
		}
		return *scaledWidth != 0 && *scaledHeight != 0;
	}
}
