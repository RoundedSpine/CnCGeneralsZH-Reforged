// Opt-in particle diagnostic snapshots shared by the SDL frame and the particle
// manager. Publishing is a few stores; the particle log reads them only while
// ParticlePerfToggle is on. Negative times mean the source was not available.
#pragma once
namespace ParticlePerfStats {
struct GpuWindow {
	double presentIntervalMs = -1.0;		///< EMA of time between presents, vsync included
	double nativeParticleMs = -1.0;			///< summed calibrated Metal time for the native particle passes
	double depthCaptureMs = -1.0;			///< depth_only + msaa_depth, same window
	double heatPassMs = -1.0;					///< heat render-pass bucket, same window
	double sceneOverdraw = -1.0;				///< scene-backbuffer fragment invocations / passed
	double nativeOverdraw = -1.0;				///< native particle pass, same ratio
	unsigned samples = 0;							///< Take_Timing intervals in the window (normally 120)
};
inline GpuWindow &window()
{
	static GpuWindow value;
	return value;
}
inline void notePresentInterval(double ms)
{
	if (ms <= 0.0)
		return;
	GpuWindow &w = window();
	if (w.presentIntervalMs < 0.0)
		w.presentIntervalMs = ms;
	else if (ms > w.presentIntervalMs)
		w.presentIntervalMs = w.presentIntervalMs * 0.5 + ms * 0.5;
	else
		w.presentIntervalMs = w.presentIntervalMs * 0.9 + ms * 0.1;
}
inline void publishGpuWindow(double nativeParticleMs, double depthCaptureMs, double heatPassMs,
	double sceneOverdraw, double nativeOverdraw, unsigned samples)
{
	GpuWindow &w = window();
	w.nativeParticleMs = nativeParticleMs;
	w.depthCaptureMs = depthCaptureMs;
	w.heatPassMs = heatPassMs;
	w.sceneOverdraw = sceneOverdraw;
	w.nativeOverdraw = nativeOverdraw;
	w.samples = samples;
}
}
