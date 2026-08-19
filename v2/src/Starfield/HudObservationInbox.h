#pragma once

#include "Domain/Destination.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <optional>

class HudObservationInbox final
{
public:
    static constexpr std::size_t MaxCourseRows = 32;

    struct MovieObservation
    {
        std::uint32_t generation {0};
        std::int64_t bornTicks {0};
    };

    struct CourseObservation
    {
        std::uint32_t generation {0};
        FormID courseId {0};
        std::array<FormID, MaxCourseRows> rows;
        std::size_t rowCount {0};
        bool overflowed {false};
        std::uint64_t revision {0};
        std::int64_t publishedTicks {0};
    };

    struct Observations
    {
        std::optional<MovieObservation> movie;
        std::optional<CourseObservation> course;
    };

    void RecordMovieCreated(std::int64_t bornTicks);
    void RecordCourse(std::uint32_t generation, FormID courseId);
    void RecordCourse(std::uint32_t generation, FormID courseId, const std::array<FormID, MaxCourseRows>& rows, std::size_t rowCount, bool overflowed);
    bool IsCurrentGeneration(std::uint32_t generation);
    Observations Drain();

private:
    std::mutex m_mutex;
    std::uint32_t m_generation {0};
    std::optional<MovieObservation> m_movie;
    std::optional<CourseObservation> m_course;
    std::uint64_t m_revision {0};
};
