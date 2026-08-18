#pragma once

#include "Domain/Destination.h"

#include <cstdint>
#include <mutex>
#include <optional>

class HudObservationInbox final
{
public:
    struct MovieObservation
    {
        std::uint32_t generation {0};
        std::int64_t bornTicks {0};
    };

    struct CourseObservation
    {
        std::uint32_t generation {0};
        FormID courseId {0};
    };

    struct Observations
    {
        std::optional<MovieObservation> movie;
        std::optional<CourseObservation> course;
    };

    void RecordMovieCreated(std::int64_t bornTicks);
    void RecordCourse(std::uint32_t generation, FormID courseId);
    bool IsCurrentGeneration(std::uint32_t generation);
    Observations Drain();

private:
    std::mutex m_mutex;
    std::uint32_t m_generation {0};
    std::optional<MovieObservation> m_movie;
    std::optional<CourseObservation> m_course;
};
