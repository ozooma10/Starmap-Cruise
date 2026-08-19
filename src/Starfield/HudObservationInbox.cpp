#include "Starfield/HudObservationInbox.h"

#include <algorithm>
#include <chrono>
#include <utility>

void HudObservationInbox::RecordMovieCreated(std::int64_t bornTicks)
{
    std::lock_guard lock {m_mutex};

    m_generation++;
    if (m_generation == 0) {
        m_generation++;
    }

    m_movie = MovieObservation {
        .generation = m_generation,
        .bornTicks = bornTicks,
    };
    m_course.reset();
}

void HudObservationInbox::RecordCourse(std::uint32_t generation, FormID courseId)

{
    RecordCourse(generation, courseId, {}, 0, false);
}

void HudObservationInbox::RecordCourse(std::uint32_t generation, FormID courseId, const std::array<FormID, MaxCourseRows>& rows, std::size_t rowCount, bool overflowed)
{
    std::lock_guard lock {m_mutex};
    if (generation == 0 || generation != m_generation) {
        return;
    }

    ++m_revision;
    if (m_revision == 0) {
        ++m_revision;
    }

    CourseObservation observation {
        .generation = generation,
        .courseId = courseId,
        .rowCount = std::min(rowCount, rows.size()),
        .overflowed = overflowed || rowCount > rows.size(),
        .revision = m_revision,
        .publishedTicks = std::chrono::steady_clock::now().time_since_epoch().count(),
    };
    std::copy_n(rows.begin(), observation.rowCount, observation.rows.begin());
    m_course = std::move(observation);
}

bool HudObservationInbox::IsCurrentGeneration(std::uint32_t generation)
{
    std::lock_guard lock {m_mutex};
    return generation != 0 && generation == m_generation;
}

HudObservationInbox::Observations HudObservationInbox::Drain()
{
    std::lock_guard lock {m_mutex};

    return {
        .movie = std::exchange(m_movie, std::nullopt),
        .course = std::exchange(m_course, std::nullopt),
    };
}
