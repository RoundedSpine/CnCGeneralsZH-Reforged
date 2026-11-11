// Host checks for the particle performance policy. No device and no game data.
#include "Platform/FireLookPack.h"
#include "Platform/ParticleDrawBudget.h"
#include "Platform/ParticleGpuClass.h"
#include "Platform/ParticlePerfStats.h"
#include <cmath>
#include <cstdio>
static int failures = 0;
#define CHECK(x) do { if (!(x)) { ++failures; std::fprintf(stderr, "FAIL %s:%d %s\n", __FILE__, __LINE__, #x); } } while (0)
static bool near(float a, float b) { return std::fabs(a - b) < 1.0e-5f; }
static bool legacyEligible(const ParticleGpuClass::Input &in)
{
	if (!in.isParticle) return false;
	if (in.nativeConservative && (in.weaponTrail || in.emissive || !in.shaderAdditive)) return false;
	if (in.smudName || in.groundAligned || in.wind || in.attached) return false;
	if (!(in.shaderAlpha || in.shaderAdditive)) return false;
	if (in.fireLook || in.flipbook || in.upEmitter) return false;
	return true;
}
int main()
{
	using namespace ParticleGpuClass;
	Input additive;
	additive.isParticle = true;
	additive.shaderAdditive = true;
	CHECK(eligible(additive));
	CHECK(reasons(additive) == Ok);
	Input native = additive;
	native.nativeConservative = true;
	CHECK(eligible(native));
	native.shaderAdditive = false;
	native.shaderAlpha = true;
	CHECK(!eligible(native));
	CHECK(reasons(native) & AlphaKeptForLighting);
	native.shaderAdditive = true;
	native.shaderAlpha = false;
	native.fireLook = true;
	CHECK(reasons(native) & FireLook);
	native.fireLook = false;
	native.weaponTrail = true;
	CHECK(reasons(native) & WeaponTrail);
	native.weaponTrail = false;
	native.emissive = true;
	CHECK(reasons(native) & Emissive);
	Input windows = additive;
	windows.weaponTrail = true;
	windows.shaderAlpha = false;
	CHECK(eligible(windows));
	for (int mask = 0; mask < 256; ++mask) {
		Input in;
		in.isParticle = mask & 1;
		in.smudName = mask & 2;
		in.groundAligned = mask & 4;
		in.wind = mask & 8;
		in.attached = mask & 16;
		in.shaderAdditive = mask & 32;
		in.shaderAlpha = mask & 64;
		in.fireLook = mask & 128;
		in.nativeConservative = (mask & 3) == 3;
		CHECK(eligible(in) == legacyEligible(in));
	}
	CHECK(near(FireLookPack::ageSeed(0, 100, 100, 0), 0.01f));
	CHECK(near(FireLookPack::ageSeed(1, 100, 100, 0), 8.01f));
	CHECK(near(FireLookPack::ageSeed(1, 0, 0, 0), 8.01f));
	CHECK(near(FireLookPack::ageSeed(1, 100, 0, 0), 8.01f + 0.98f));
	CHECK(ParticleDrawBudget::pixelFloor(16.7) == 0.0f);
	CHECK(near(ParticleDrawBudget::pixelFloor(33.4), 2.25f));
	CHECK(ParticleDrawBudget::smokeKeepFraction(20.0) == 1.0f);
	CHECK(near(ParticleDrawBudget::smokeKeepFraction(60.0), 0.5f));
	CHECK(ParticleDrawBudget::classify(true, 0, 0, 0, 1, 10, true, 1, 4, 0, false) == ParticleDrawBudget::CullInvisible);
	CHECK(ParticleDrawBudget::classify(false, 1, 1, 1, 0, 10, true, 1, 4, 0, false) == ParticleDrawBudget::CullInvisible);
	CHECK(ParticleDrawBudget::classify(false, 1, 1, 1, 0, 10, true, 1, 4, 0, true) == ParticleDrawBudget::Keep);
	CHECK(ParticleDrawBudget::classify(false, 1, 1, 1, 1, 0.2f, true, 0.75f, 4, 0, false) == ParticleDrawBudget::CullSmall);
	CHECK(ParticleDrawBudget::classify(false, 1, 1, 1, 1, 10, true, 0, 1, 2, false) == ParticleDrawBudget::CullDensity);
	CHECK(ParticleDrawBudget::classify(true, 1, 1, 1, 1, 10, true, 0, 4, 2, false) == ParticleDrawBudget::Keep);
	ParticlePerfStats::notePresentInterval(10);
	ParticlePerfStats::notePresentInterval(30);
	CHECK(ParticlePerfStats::window().presentIntervalMs > 10.0);
	ParticlePerfStats::publishGpuWindow(1.5, 0.25, -1, 3.5, -1, 120);
	CHECK(ParticlePerfStats::window().nativeParticleMs == 1.5);
	CHECK(ParticlePerfStats::window().depthCaptureMs == 0.25);
	CHECK(ParticlePerfStats::window().heatPassMs < 0.0);
	CHECK(ParticlePerfStats::window().sceneOverdraw == 3.5);
	if (failures) {
		std::fprintf(stderr, "%d particle policy checks failed\n", failures);
		return 1;
	}
	std::printf("particle policy checks passed\n");
	return 0;
}
