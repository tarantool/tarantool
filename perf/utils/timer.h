#pragma once

#include <chrono>

// Primitive wrapper to measure execution time.
class PerfTimer {
public:
	PerfTimer() {};
	void start()
	{
		start_point = std::chrono::high_resolution_clock::now();
	}
	void stop()
	{
		stop_point = std::chrono::high_resolution_clock::now();
	}
	double elapsed_ms() const
	{
		auto time =
			std::chrono::duration_cast<std::chrono::milliseconds>(stop_point - start_point);
		return time.count();
	}

private:
	std::chrono::high_resolution_clock::time_point start_point;
	std::chrono::high_resolution_clock::time_point stop_point;
};
