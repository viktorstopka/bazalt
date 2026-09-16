#pragma once

#include <algorithm>
#include <cmath>

namespace bazalt::engine
{
    /** Simple asymmetric peak-meter ballistics (fast attack, slower
        release), applied once per analysis drain cycle rather than per
        sample — the coefficient is derived from the actual elapsed time
        span of each call (not a fixed sample-rate-based constant), so the
        smoothing stays correct even if the analysis thread's drain
        interval jitters (ARCHITECTURE.md §6.2's "meter/envelope snapshots
        with proper ballistics").
    */
    class MeterBallistics
    {
    public:
        void setTimes (double attackSecondsIn, double releaseSecondsIn) noexcept
        {
            attackSeconds = attackSecondsIn;
            releaseSeconds = releaseSecondsIn;
        }

        void reset() noexcept { envelope = 0.0f; }

        float pushPeak (float instantaneousPeak, double elapsedSeconds) noexcept
        {
            const auto tau = instantaneousPeak > envelope ? attackSeconds : releaseSeconds;
            const auto coeff = tau > 0.0 ? (float) std::exp (-elapsedSeconds / tau) : 0.0f;
            envelope = coeff * envelope + (1.0f - coeff) * instantaneousPeak;
            return envelope;
        }

        float getCurrentValue() const noexcept { return envelope; }

    private:
        double attackSeconds = 0.001;
        double releaseSeconds = 0.3;
        float envelope = 0.0f;
    };
}
