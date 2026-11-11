// Client draw budget for transparent particles. Invisible and off-screen work
// is dropped with no picture change. Size and smoke-density cuts engage only
// as measured present interval grows past the targets below, and only while
// the budget is enabled. ALWAYS_RENDER is never cut.
#pragma once
namespace ParticleDrawBudget {
enum Decision { Keep = 0, CullInvisible = 1, CullSmall = 2, CullDensity = 3 };
inline constexpr double kPresentTargetMs = 16.7;
inline constexpr double kDensityStartMs = 20.0;
// Below this fraction of the system's largest visible particle, slow frames
// drop alpha filler. 1 keeps every size; 0.5 keeps particles at least half
// as large as the biggest one in that system.
inline float smokeKeepFraction(double presentMs)
{
	if (presentMs <= kDensityStartMs)
		return 1.0f;
	double over = (presentMs - kDensityStartMs) / 40.0;
	if (over > 0.5)
		over = 0.5;
	return 1.0f - (float)over;
}
// 0 disables the screen-size cut. Rises from 0.75px once a frame misses 60Hz.
inline float pixelFloor(double presentMs)
{
	if (presentMs <= kPresentTargetMs)
		return 0.0f;
	double over = (presentMs - kPresentTargetMs) / kPresentTargetMs;
	if (over > 3.0)
		over = 3.0;
	return (float)(0.75 + over * 1.5);
}
inline Decision classify(bool additive, float red, float green, float blue, float alpha,
	float pixels, bool pixelsKnown, float floorPx, float size, float minKeepSize, bool protect)
{
	if (protect)
		return Keep;
	if (!additive && alpha <= 0.0f)
		return CullInvisible;
	if (additive && red <= 0.0f && green <= 0.0f && blue <= 0.0f)
		return CullInvisible;
	if (pixelsKnown && floorPx > 0.0f && pixels < floorPx)
		return CullSmall;
	if (minKeepSize > 0.0f && size < minKeepSize)
		return CullDensity;
	return Keep;
}
}
