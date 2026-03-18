#pragma once

// Frame-aware profiler for ESP32 — tracks per-section timing within each
// frame and reports avg/max/min plus percentage of total frame time.
//
// Comment out the next line to disable profiling (zero overhead).
#define PROFILING_ENABLED

#include <Arduino.h>

#ifdef PROFILING_ENABLED

class FrameProfiler {
private:
    struct Section {
        const char* name;
        uint32_t totalUs;
        uint32_t maxUs;
        uint32_t minUs;
        uint32_t callCount;
    };

    static const int MAX_SECTIONS = 12;
    Section sections[MAX_SECTIONS];
    int sectionCount = 0;

    uint32_t sectionStartUs = 0;
    int currentSection = -1;

    // Frame-level tracking
    uint32_t frameStartUs = 0;
    uint32_t frameTotalUs = 0;
    uint32_t frameMaxUs = 0;
    uint32_t frameMinUs = UINT32_MAX;
    uint32_t frameCount = 0;

public:
    FrameProfiler() {
        for (int i = 0; i < MAX_SECTIONS; i++) {
            sections[i].name = nullptr;
            sections[i].totalUs = 0;
            sections[i].maxUs = 0;
            sections[i].minUs = UINT32_MAX;
            sections[i].callCount = 0;
        }
    }

    void beginFrame() {
        frameStartUs = micros();
    }

    void endFrame() {
        uint32_t elapsed = micros() - frameStartUs;
        frameTotalUs += elapsed;
        frameCount++;
        if (elapsed > frameMaxUs) frameMaxUs = elapsed;
        if (elapsed < frameMinUs) frameMinUs = elapsed;
    }

    void start(const char* sectionName) {
        // Find existing section by pointer (string literals have stable addresses)
        currentSection = -1;
        for (int i = 0; i < sectionCount; i++) {
            if (sections[i].name == sectionName) {
                currentSection = i;
                break;
            }
        }
        if (currentSection == -1 && sectionCount < MAX_SECTIONS) {
            currentSection = sectionCount++;
            sections[currentSection].name = sectionName;
            sections[currentSection].totalUs = 0;
            sections[currentSection].maxUs = 0;
            sections[currentSection].minUs = UINT32_MAX;
            sections[currentSection].callCount = 0;
        }
        sectionStartUs = micros();
    }

    void end() {
        if (currentSection >= 0) {
            uint32_t elapsed = micros() - sectionStartUs;
            Section& s = sections[currentSection];
            s.totalUs += elapsed;
            s.callCount++;
            if (elapsed > s.maxUs) s.maxUs = elapsed;
            if (elapsed < s.minUs) s.minUs = elapsed;
            currentSection = -1;
        }
    }

    // Inject a pre-measured duration (avoids start/end overhead in hot loops)
    void accumulateUs(const char* sectionName, uint32_t us) {
        int idx = -1;
        for (int i = 0; i < sectionCount; i++) {
            if (sections[i].name == sectionName) { idx = i; break; }
        }
        if (idx == -1 && sectionCount < MAX_SECTIONS) {
            idx = sectionCount++;
            sections[idx].name = sectionName;
            sections[idx].totalUs = 0;
            sections[idx].maxUs = 0;
            sections[idx].minUs = UINT32_MAX;
            sections[idx].callCount = 0;
        }
        if (idx >= 0) {
            Section& s = sections[idx];
            s.totalUs += us;
            s.callCount++;
            if (us > s.maxUs) s.maxUs = us;
            if (us < s.minUs) s.minUs = us;
        }
    }

    // Helper: print one line and flush to avoid USB CDC data loss
    void printLine(const char* line) {
        Serial.println(line);
        Serial.flush();
    }

    void printReport() {
        if (frameCount == 0) return;

        float durationSec = frameTotalUs / 1000000.0f;
        uint32_t frameAvg = frameTotalUs / frameCount;
        float fps = (durationSec > 0) ? frameCount / durationSec : 0;

        char line[80];

        snprintf(line, sizeof(line), "=== Frame Profile (%lu frames, %.1fs) ===",
                 (unsigned long)frameCount, durationSec);
        printLine(line);

        snprintf(line, sizeof(line), "Frame: %lu avg | %lu max | %lu min us (%.1f fps)",
                 (unsigned long)frameAvg,
                 (unsigned long)frameMaxUs,
                 (unsigned long)(frameMinUs == UINT32_MAX ? 0 : frameMinUs),
                 fps);
        printLine(line);

        printLine("--------------------------------------------------------");
        printLine("Section              Avg(us)  Max(us)  Min(us)  %Frame");

        float accountedPct = 0;
        for (int i = 0; i < sectionCount; i++) {
            Section& s = sections[i];
            if (s.callCount == 0) continue;
            uint32_t avg = s.totalUs / s.callCount;
            uint32_t minVal = (s.minUs == UINT32_MAX) ? 0 : s.minUs;
            float pct = (frameAvg > 0) ? (float)avg / frameAvg * 100.0f : 0;
            if (s.callCount < frameCount) {
                pct = (frameAvg > 0) ? ((float)s.totalUs / frameCount) / frameAvg * 100.0f : 0;
            }
            accountedPct += pct;
            snprintf(line, sizeof(line), "%-20s %7lu  %7lu  %7lu  %5.1f",
                     s.name,
                     (unsigned long)avg,
                     (unsigned long)s.maxUs,
                     (unsigned long)minVal,
                     pct);
            printLine(line);
        }
        snprintf(line, sizeof(line), "%-20s                           %5.1f%%",
                 "total:", accountedPct);
        printLine(line);
        printLine("========================================================");
    }

    void reset() {
        for (int i = 0; i < sectionCount; i++) {
            sections[i].totalUs = 0;
            sections[i].maxUs = 0;
            sections[i].minUs = UINT32_MAX;
            sections[i].callCount = 0;
        }
        frameTotalUs = 0;
        frameMaxUs = 0;
        frameMinUs = UINT32_MAX;
        frameCount = 0;
    }
};

extern FrameProfiler profiler;

#define PROFILE_FRAME_BEGIN() profiler.beginFrame()
#define PROFILE_FRAME_END()   profiler.endFrame()
#define PROFILE_START(name)   profiler.start(name)
#define PROFILE_END()         profiler.end()
#define PROFILE_ACCUMULATE(name, us) profiler.accumulateUs(name, us)
#define PROFILE_REPORT()      profiler.printReport()
#define PROFILE_RESET()       profiler.reset()

#else // PROFILING_ENABLED not defined

#define PROFILE_FRAME_BEGIN() ((void)0)
#define PROFILE_FRAME_END()   ((void)0)
#define PROFILE_START(name)   ((void)0)
#define PROFILE_END()         ((void)0)
#define PROFILE_ACCUMULATE(name, us) ((void)0)
#define PROFILE_REPORT()      ((void)0)
#define PROFILE_RESET()       ((void)0)

#endif // PROFILING_ENABLED
