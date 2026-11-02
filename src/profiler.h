#ifndef PROFILER_H
#define PROFILER_H

#include "headers.h"
#include <array>
#include <chrono>

// Render passes timed on the GPU. Passes run one after another (GL_TIME_ELAPSED
// queries cannot nest), so the order here is the order they are issued.
enum ProfilePass
{
	PASS_SHADOW = 0,
	PASS_MESH,
	PASS_LIGHTS,
	PASS_SKYBOX,
	NUM_PROFILE_PASSES
};

// One finished frame. GPU times arrive a few frames late (see FrameProfiler).
struct FrameSample
{
	int frame = 0;
	double cpuFrameMs = 0.0;	// wall time from this frame's start to the next frame's start
	double cpuRenderMs = 0.0;	// CPU time spent issuing GL calls for this frame (before swap)
	std::array<double, NUM_PROFILE_PASSES> gpuPassMs{};
	double gpuTotalMs = 0.0;
	// Synchronous mode only: wall time of each pass bracketed by glFinish, and
	// of the whole frame including GPU completion. 0 when not measured.
	std::array<double, NUM_PROFILE_PASSES> syncPassMs{};
	double syncFrameMs = 0.0;
	int drawCalls = 0;
	long long triangles = 0;
};

// CPU timers (std::chrono::steady_clock) plus GPU timers (GL_TIME_ELAPSED queries).
// Query results are read kLatency frames later so reading them never stalls the
// pipeline waiting for the GPU.
class FrameProfiler
{
public:
	FrameProfiler();
	~FrameProfiler();

	void BeginFrame();
	void EndFrame();	// call right before SwapBuffers
	void BeginPass(ProfilePass pass);
	void EndPass(ProfilePass pass);
	void CountDraw(long long numTriangles) { ++current.drawCalls; current.triangles += numTriangles; }

	// Waits for outstanding queries and returns every completed frame so far.
	const std::vector<FrameSample>& Finish();
	const std::vector<FrameSample>& GetSamples() const { return samples; }
	void ClearSamples() { samples.clear(); }

	static const char* PassName(int pass);

	// Synchronous timing: glFinish around every pass and at the end of the frame.
	// Slower, but the numbers do not depend on how the driver attributes GPU work
	// to timer queries (unreliable per pass on tile-based GPUs).
	void SetSynchronous(bool on) { synchronous = on; }
	bool IsSynchronous() const { return synchronous; }

private:
	using Clock = std::chrono::steady_clock;
	static const int kLatency = 4;

	struct Slot
	{
		bool pending = false;
		FrameSample sample;
		std::array<GLuint, NUM_PROFILE_PASSES> queries{};
		std::array<bool, NUM_PROFILE_PASSES> used{};
	};

	void Collect(Slot& slot);

	std::array<Slot, kLatency> slots;
	std::vector<FrameSample> samples;
	FrameSample current;
	int frameIndex;
	Clock::time_point frameStart;
	Clock::time_point passStart;
	bool synchronous = false;
	Clock::time_point lastFrameStart;
	bool hasLastFrame;
};

// Summary statistics over a series of values (milliseconds, counts, ...).
struct Stats
{
	double mean = 0.0, median = 0.0, p95 = 0.0, min = 0.0, max = 0.0;
};
Stats ComputeStats(std::vector<double> values);

// Writes one CSV row per sample and prints mean / median / p95 per column.
bool WriteBenchCsv(const std::string& path, const std::vector<FrameSample>& samples);
void PrintBenchSummary(std::ostream& os, const std::vector<FrameSample>& samples);

#endif
