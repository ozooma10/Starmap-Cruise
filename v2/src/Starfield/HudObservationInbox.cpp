#include "Starfield/HudObservationInbox.h"

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
    std::lock_guard lock {m_mutex};
    if (generation == 0 || generation != m_generation) {
        return;
    }

    m_coursePublication++;
    if (m_coursePublication == 0) {
        m_coursePublication++;
    }

    m_course = CourseObservation {
        .generation = generation,
        .courseId = courseId,
        .publication = m_coursePublication,
    };
}

bool HudObservationInbox::IsCurrentGeneration(std::uint32_t generation)
{
    std::lock_guard lock {m_mutex};
    return generation != 0 && generation == m_generation;
}

std::uint64_t HudObservationInbox::LatestCoursePublication()
{
    std::lock_guard lock {m_mutex};
    return m_coursePublication;
}

HudObservationInbox::Observations HudObservationInbox::Drain()
{
    std::lock_guard lock {m_mutex};

    return {
        .movie = std::exchange(m_movie, std::nullopt),
        .course = std::exchange(m_course, std::nullopt),
    };
}
