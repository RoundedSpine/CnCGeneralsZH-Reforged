// FireLook age/seed packed into billboard v2. Kept identical to the historical
// expression in W3DParticleSys fillBillboards so a GPU port can be compared
// against it. The native particle shader does not sample this channel.
#pragma once
namespace FireLookPack {
inline float ageSeed(unsigned personality, unsigned lifetime, unsigned lifetimeLeft, unsigned flipbookLog2)
{
	const float age = lifetime ? 1.0f - (float)lifetimeLeft / (float)lifetime : 0.0f;
	return (float)((((personality * 97u) & 31u) << 3) | flipbookLog2) + 0.01f + 0.98f * age;
}
}
