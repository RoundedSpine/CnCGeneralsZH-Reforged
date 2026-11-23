// CPU oracle for camera-only motion vectors. No GPU. GPL-3.0; see LICENSE.md.
#include "CameraMotion.h"

#include <cmath>
#include <cstdio>
#include <cstring>

static int failures = 0;

static void expect(bool condition, const char *what)
{
	if (!condition) {
		std::fprintf(stderr, "FAIL %s\n", what);
		++failures;
	}
}

static void expect_near(float value, float expected, float tolerance, const char *what)
{
	if (!std::isfinite(value) || std::fabs(value - expected) > tolerance) {
		std::fprintf(stderr, "FAIL %s got %.6f expected %.6f\n", what, value, expected);
		++failures;
	}
}

// D3D left-handed perspective, row-major, row-vector clip.w = z.
static void perspective(float m[16], float xScale, float yScale, float zn, float zf)
{
	std::memset(m, 0, sizeof(float) * 16);
	m[0] = xScale;
	m[5] = yScale;
	m[10] = zf / (zf - zn);
	m[11] = 1.0f;
	m[14] = -zn * zf / (zf - zn);
}

static void view_at(float m[16], float x, float y, float z)
{
	std::memset(m, 0, sizeof(float) * 16);
	m[0] = m[5] = m[10] = m[15] = 1.0f;
	m[12] = -x;
	m[13] = -y;
	m[14] = -z;
}

static void full_view(CameraMotion::View &view, const float clip[16], float minZ, float maxZ, unsigned width, unsigned height)
{
	std::memset(&view, 0, sizeof(view));
	std::memcpy(view.ClipFromWorld, clip, sizeof(view.ClipFromWorld));
	view.MinZ = minZ;
	view.MaxZ = maxZ;
	view.Viewport[2] = static_cast<float>(width);
	view.Viewport[3] = static_cast<float>(height);
	view.Width = width;
	view.Height = height;
}

static float window_depth(const float clip[16], float viewX, float viewY, float viewZ, float minZ, float maxZ)
{
	const float clipX = viewX * clip[0] + viewY * clip[4] + viewZ * clip[8] + clip[12];
	const float clipY = viewX * clip[1] + viewY * clip[5] + viewZ * clip[9] + clip[13];
	const float clipZ = viewX * clip[2] + viewY * clip[6] + viewZ * clip[10] + clip[14];
	const float clipW = viewX * clip[3] + viewY * clip[7] + viewZ * clip[11] + clip[15];
	(void)clipX;
	(void)clipY;
	const float ndcZ = clipZ / clipW;
	return minZ + ndcZ * (maxZ - minZ);
}

int main()
{
	float projection[16];
	perspective(projection, 1.5f, 1.5f, 1.0f, 100.0f);
	const unsigned width = 200, height = 100;
	float identity[16], currentClip[16], previousClip[16];
	view_at(identity, 0, 0, 0);
	multiply(identity, projection, previousClip);

	CameraMotion::View previous;
	full_view(previous, previousClip, 0.0f, 1.0f, width, height);
	CameraMotion::Constants none;
	expect(CameraMotion::Make(previous, nullptr, none), "make without history");
	expect(none.DepthRemap[0] == 0.0f, "first frame has no history");
	float mx = 1, my = 1;
	CameraMotion::Vector(none, 100.0f, 50.0f, 0.5f, mx, my);
	expect(mx == 0.0f && my == 0.0f, "no history writes zero");

	CameraMotion::Constants same;
	expect(CameraMotion::Make(previous, &previous, same), "make with matching history");
	expect(same.DepthRemap[0] == 1.0f, "matching camera keeps history");

	// A static point that projects to the centre of a 200x100 target. Window depth is
	// the perspective depth of view-space (0, 0, 10). The camera did not move.
	const float centreDepth = window_depth(previousClip, 0, 0, 10.0f, 0, 1);
	CameraMotion::Vector(same, 100.0f, 50.0f, centreDepth, mx, my);
	expect_near(mx, 0.0f, 1.0e-3f, "still centre x");
	expect_near(my, 0.0f, 1.0e-3f, "still centre y");

	// An off-centre pixel. Reconstruct its view-space point from NDC, then read it back.
	const float pixelX = 40.5f, pixelY = 20.5f;
	const float uvX = pixelX / width, uvY = pixelY / height;
	const float ndcX = uvX * 2.0f - 1.0f, ndcY = 1.0f - uvY * 2.0f;
	const float z = 10.0f;
	const float viewX = ndcX / 1.5f * z, viewY = ndcY / 1.5f * z;
	const float offDepth = window_depth(previousClip, viewX, viewY, z, 0, 1);
	CameraMotion::Vector(same, pixelX, pixelY, offDepth, mx, my);
	expect_near(mx, 0.0f, 2.0e-3f, "still off-centre x");
	expect_near(my, 0.0f, 2.0e-3f, "still off-centre y");

	// Camera translates +1.3 on X. The world origin-depth point that was at the centre
	// now projects to view x = -1.3, ndc.x = -1.3 * 1.5 / 10 = -0.195, uv.x = 0.4025.
	// That is target position 80.5. Motion is current position minus the old centre.
	float moved[16];
	view_at(moved, 1.3f, 0, 0);
	multiply(moved, projection, currentClip);
	CameraMotion::View current;
	full_view(current, currentClip, 0.0f, 1.0f, width, height);
	CameraMotion::Constants translated;
	expect(CameraMotion::Make(current, &previous, translated), "make translated");
	const float movedDepth = window_depth(currentClip, 0.0f, 0.0f, 10.0f, 0, 1);
	CameraMotion::Vector(translated, 80.5f, 50.0f, movedDepth, mx, my);
	expect_near(mx, (0.4025f - 0.5f) * 200.0f, 0.05f, "pan x pixels");
	expect_near(my, 0.0f, 0.05f, "pan y stays");

	// The same point seen from behind the previous camera (camera at z=50, point at z=10).
	float behindView[16], behindClip[16];
	view_at(behindView, 0, 0, 50.0f);
	multiply(behindView, projection, behindClip);
	CameraMotion::View behind;
	full_view(behind, behindClip, 0.0f, 1.0f, width, height);
	CameraMotion::Constants behindConstants;
	expect(CameraMotion::Make(current, &behind, behindConstants), "make behind");
	CameraMotion::Vector(behindConstants, 80.5f, 50.0f, movedDepth, mx, my);
	expect(mx == 0.0f && my == 0.0f, "point behind the previous camera is zero");

	CameraMotion::Vector(translated, 80.5f, 50.0f, 1.0f, mx, my);
	expect(mx == 0.0f && my == 0.0f, "far-plane sky is zero");
	CameraMotion::Vector(translated, -1.0f, 50.0f, movedDepth, mx, my);
	expect(mx == 0.0f && my == 0.0f, "outside the viewport is zero");

	// A viewport that does not cover the whole target, and a depth range that is not 0..1.
	// Stationary camera: the remapped window depth still round-trips to zero motion.
	CameraMotion::View ranged = previous;
	ranged.MinZ = 0.25f;
	ranged.MaxZ = 0.75f;
	ranged.Viewport[0] = 10.0f;
	ranged.Viewport[1] = 4.0f;
	ranged.Viewport[2] = 180.0f;
	ranged.Viewport[3] = 90.0f;
	CameraMotion::Constants rangedConstants;
	expect(CameraMotion::Make(ranged, &ranged, rangedConstants), "make ranged");
	const float rangedDepth = window_depth(previousClip, 0, 0, 10.0f, 0.25f, 0.75f);
	const float centreX = 10.0f + 0.5f * 180.0f;
	const float centreY = 4.0f + 0.5f * 90.0f;
	CameraMotion::Vector(rangedConstants, centreX, centreY, rangedDepth, mx, my);
	expect_near(mx, 0.0f, 2.0e-3f, "remapped depth still centre x");
	expect_near(my, 0.0f, 2.0e-3f, "remapped depth still centre y");

	CameraMotion::View resized = previous;
	resized.Width = width / 2;
	CameraMotion::Constants resizedConstants;
	expect(CameraMotion::Make(resized, &previous, resizedConstants), "make resized");
	expect(resizedConstants.DepthRemap[0] == 0.0f, "a size change drops history");

	if (failures) {
		std::fprintf(stderr, "%d camera-motion checks failed\n", failures);
		return 1;
	}
	std::printf("camera motion ok\n");
	return 0;
}
