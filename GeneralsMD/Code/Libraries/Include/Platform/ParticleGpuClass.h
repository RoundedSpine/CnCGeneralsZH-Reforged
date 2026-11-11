// Why a particle template stays off the native Metal particle path.
// eligible() must match the historical boolean in ParticleSystemTemplate::isGpuEligible.
#pragma once
#include <cstdlib>
#include <cstring>
namespace ParticleGpuClass {
enum Reason : unsigned {
	Ok = 0,
	NotParticle = 1u << 0,
	SmudTexture = 1u << 1,
	GroundAligned = 1u << 2,
	Wind = 1u << 3,
	AttachedChild = 1u << 4,
	ShaderUnsupported = 1u << 5,		///< not ALPHA or ADDITIVE (sorted CPU pool / other draws)
	FireLook = 1u << 6,							///< per-vertex age/seed; the GPU draw has no FireLook sampling
	Flipbook = 1u << 7,
	UpTowardsEmitter = 1u << 8,
	WeaponTrail = 1u << 9,					///< native only: iOS contact-fade exemption is CPU-side
	Emissive = 1u << 10,						///< native only: emissive shade is a CPU/shader flag the GPU record lacks
	AlphaKeptForLighting = 1u << 11	///< native only: smoke lighting and combined sort stay on the CPU
};
struct Input {
	bool isParticle = false;
	bool smudName = false;
	bool groundAligned = false;
	bool wind = false;
	bool attached = false;
	bool shaderAdditive = false;
	bool shaderAlpha = false;
	bool fireLook = false;
	bool flipbook = false;
	bool upEmitter = false;
	bool weaponTrail = false;
	bool emissive = false;
	bool nativeConservative = false;
};
inline unsigned reasons(const Input &in)
{
	unsigned why = Ok;
	if (!in.isParticle)
		why |= NotParticle;
	if (in.smudName)
		why |= SmudTexture;
	if (in.groundAligned)
		why |= GroundAligned;
	if (in.wind)
		why |= Wind;
	if (in.attached)
		why |= AttachedChild;
	if (!(in.shaderAlpha || in.shaderAdditive))
		why |= ShaderUnsupported;
	if (in.fireLook)
		why |= FireLook;
	if (in.flipbook)
		why |= Flipbook;
	if (in.upEmitter)
		why |= UpTowardsEmitter;
	if (in.nativeConservative) {
		if (in.weaponTrail)
			why |= WeaponTrail;
		if (in.emissive)
			why |= Emissive;
		if (!in.shaderAdditive)
			why |= AlphaKeptForLighting;
	}
	return why;
}
inline bool eligible(const Input &in) { return reasons(in) == Ok; }
// Opt-in: eligible additive particles spawn on the GPU even below the CPU cap.
// Default off. Composite order against CPU-sorted smoke is not yet device-proven.
inline bool additiveOffloadRequested()
{
	const char *env = std::getenv("ZH_GPU_OFFLOAD_ADDITIVE");
	return env && std::strcmp(env, "1") == 0;
}
}
