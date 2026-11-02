#include "profiler.h"

#include <algorithm>
#include <cmath>
#include <functional>

namespace {

double MsBetween(std::chrono::steady_clock::time_point a, std::chrono::steady_clock::time_point b)
{
	return std::chrono::duration<double, std::milli>(b - a).count();
}

}

FrameProfiler::FrameProfiler()
	: frameIndex(0), hasLastFrame(false)
{
	for (Slot& slot : slots)
		glGenQueries(NUM_PROFILE_PASSES, slot.queries.data());
}

FrameProfiler::~FrameProfiler()
{
	for (Slot& slot : slots)
		glDeleteQueries(NUM_PROFILE_PASSES, slot.queries.data());
}

const char* FrameProfiler::PassName(int pass)
{
	switch (pass) {
	case PASS_SHADOW: return "shadow";
	case PASS_MESH:   return "mesh";
	case PASS_LIGHTS: return "lights";
	case PASS_SKYBOX: return "skybox";
	default:          return "unknown";
	}
}

void FrameProfiler::BeginFrame()
{
	frameStart = Clock::now();
	// The previous frame's wall time is only known now.
	if (hasLastFrame)
		slots[(frameIndex - 1) % kLatency].sample.cpuFrameMs = MsBetween(lastFrameStart, frameStart);
	lastFrameStart = frameStart;
	hasLastFrame = true;

	// Reuse the oldest slot; its queries were issued kLatency frames ago.
	Slot& slot = slots[frameIndex % kLatency];
	if (slot.pending)
		Collect(slot);

	current = FrameSample();
	current.frame = frameIndex;
	if (synchronous)
		glFinish();	// start from an idle GPU
}

void FrameProfiler::BeginPass(ProfilePass pass)
{
	Slot& slot = slots[frameIndex % kLatency];
	if (synchronous) {
		glFinish();
		passStart = Clock::now();
	}
	glBeginQuery(GL_TIME_ELAPSED, slot.queries[pass]);
	slot.used[pass] = true;
}

void FrameProfiler::EndPass(ProfilePass pass)
{
	glEndQuery(GL_TIME_ELAPSED);
	if (synchronous) {
		glFinish();
		current.syncPassMs[pass] += MsBetween(passStart, Clock::now());
	}
}

void FrameProfiler::EndFrame()
{
	Slot& slot = slots[frameIndex % kLatency];
	if (synchronous) {
		glFinish();
		current.syncFrameMs = MsBetween(frameStart, Clock::now());
	}
	current.cpuRenderMs = MsBetween(frameStart, Clock::now());
	slot.sample = current;
	slot.pending = true;
	++frameIndex;
}

void FrameProfiler::Collect(Slot& slot)
{
	slot.sample.gpuTotalMs = 0.0;
	for (int p = 0; p < NUM_PROFILE_PASSES; ++p) {
		if (!slot.used[p])
			continue;
		GLuint64 ns = 0;
		glGetQueryObjectui64v(slot.queries[p], GL_QUERY_RESULT, &ns);
		slot.sample.gpuPassMs[p] = ns / 1.0e6;
		slot.sample.gpuTotalMs += slot.sample.gpuPassMs[p];
		slot.used[p] = false;
	}
	samples.push_back(slot.sample);
	slot.pending = false;
}

const std::vector<FrameSample>& FrameProfiler::Finish()
{
	// Oldest pending slot first, so samples stay in frame order.
	for (int i = 0; i < kLatency; ++i) {
		Slot& slot = slots[(frameIndex + i) % kLatency];
		if (slot.pending)
			Collect(slot);
	}
	return samples;
}

Stats ComputeStats(std::vector<double> values)
{
	Stats s;
	if (values.empty())
		return s;
	std::sort(values.begin(), values.end());
	double sum = 0.0;
	for (double v : values)
		sum += v;
	auto percentile = [&](double q) {
		// Nearest-rank percentile.
		size_t rank = (size_t)std::ceil(q * values.size());
		return values[std::min(values.size(), std::max<size_t>(rank, 1)) - 1];
	};
	s.mean = sum / values.size();
	s.median = percentile(0.5);
	s.p95 = percentile(0.95);
	s.min = values.front();
	s.max = values.back();
	return s;
}

bool WriteBenchCsv(const std::string& path, const std::vector<FrameSample>& samples)
{
	std::ofstream ofs(path);
	if (!ofs)
		return false;
	ofs << "frame,cpu_frame_ms,cpu_render_ms";
	for (int p = 0; p < NUM_PROFILE_PASSES; ++p)
		ofs << ",gpu_" << FrameProfiler::PassName(p) << "_ms";
	ofs << ",gpu_total_ms";
	for (int p = 0; p < NUM_PROFILE_PASSES; ++p)
		ofs << ",sync_" << FrameProfiler::PassName(p) << "_ms";
	ofs << ",sync_frame_ms,draw_calls,triangles\n";
	ofs << std::fixed << std::setprecision(4);
	for (const FrameSample& s : samples) {
		ofs << s.frame << "," << s.cpuFrameMs << "," << s.cpuRenderMs;
		for (int p = 0; p < NUM_PROFILE_PASSES; ++p)
			ofs << "," << s.gpuPassMs[p];
		ofs << "," << s.gpuTotalMs;
		for (int p = 0; p < NUM_PROFILE_PASSES; ++p)
			ofs << "," << s.syncPassMs[p];
		ofs << "," << s.syncFrameMs << "," << s.drawCalls << "," << s.triangles << "\n";
	}
	return true;
}

void PrintBenchSummary(std::ostream& os, const std::vector<FrameSample>& samples)
{
	auto column = [&](std::function<double(const FrameSample&)> get) {
		std::vector<double> v;
		for (const FrameSample& s : samples)
			v.push_back(get(s));
		return ComputeStats(v);
	};
	auto row = [&](const std::string& name, const Stats& st) {
		os << "  " << std::left << std::setw(16) << name << std::right << std::fixed << std::setprecision(3)
		   << std::setw(9) << st.mean << std::setw(9) << st.median << std::setw(9) << st.p95
		   << std::setw(9) << st.min << std::setw(9) << st.max << "\n";
	};
	os << "Frames: " << samples.size() << "\n";
	os << "  " << std::left << std::setw(16) << "ms" << std::right
	   << std::setw(9) << "mean" << std::setw(9) << "median" << std::setw(9) << "p95"
	   << std::setw(9) << "min" << std::setw(9) << "max" << "\n";
	row("cpu_frame", column([](const FrameSample& s) { return s.cpuFrameMs; }));
	row("cpu_render", column([](const FrameSample& s) { return s.cpuRenderMs; }));
	for (int p = 0; p < NUM_PROFILE_PASSES; ++p)
		row(std::string("gpu_") + FrameProfiler::PassName(p), column([p](const FrameSample& s) { return s.gpuPassMs[p]; }));
	row("gpu_total", column([](const FrameSample& s) { return s.gpuTotalMs; }));
	if (!samples.empty() && samples.back().syncFrameMs > 0.0) {
		for (int p = 0; p < NUM_PROFILE_PASSES; ++p)
			row(std::string("sync_") + FrameProfiler::PassName(p), column([p](const FrameSample& s) { return s.syncPassMs[p]; }));
		row("sync_frame", column([](const FrameSample& s) { return s.syncFrameMs; }));
	}
	if (!samples.empty())
		os << "  draw calls/frame: " << samples.back().drawCalls
		   << ", triangles/frame: " << samples.back().triangles << "\n";
}
