#include "Starfield/HudObservationInbox.h"
#include "TestSuites.h"

#include <stdexcept>
#include <string>
#include <string_view>

namespace
{
    void Require(bool condition, std::string_view message)
    {
        if (!condition) {
            throw std::runtime_error {std::string {message}};
        }
    }

    void TestMoviePublishesOneCoherentObservation()
    {
        HudObservationInbox inbox;
        inbox.RecordMovieCreated(100);

        const auto observations = inbox.Drain();
        Require(observations.movie.has_value(), "movie creation was not recorded");
        Require(observations.movie->generation == 1, "movie retained the wrong generation");
        Require(observations.movie->bornTicks == 100, "movie retained the wrong creation time");
        Require(inbox.IsCurrentGeneration(1), "drain invalidated the current generation");
        Require(!inbox.Drain().movie, "movie creation was delivered more than once");
    }

    void TestLatestCourseReplacesStaleValue()
    {
        HudObservationInbox inbox;
        inbox.RecordMovieCreated(100);
        const auto generation = inbox.Drain().movie->generation;

        inbox.RecordCourse(generation, 0x1234);
        inbox.RecordCourse(generation, 0x5678);

        const auto observations = inbox.Drain();
        Require(observations.course.has_value(), "course was not recorded");
        Require(observations.course->generation == generation, "course retained the wrong generation");
        Require(observations.course->courseId == 0x5678, "latest course did not replace the stale value");
    }

    void TestReplacementRejectsOldGenerationBeforeDrain()
    {
        HudObservationInbox inbox;
        inbox.RecordMovieCreated(100);
        const auto oldGeneration = inbox.Drain().movie->generation;

        inbox.RecordCourse(oldGeneration, 0x1234);
        inbox.RecordMovieCreated(200);

        Require(!inbox.IsCurrentGeneration(oldGeneration), "replacement retained the old generation");
        inbox.RecordCourse(oldGeneration, 0x5678);

        const auto observations = inbox.Drain();
        Require(observations.movie.has_value(), "replacement movie was not recorded");
        Require(observations.movie->generation == 2, "replacement retained the wrong generation");
        Require(observations.movie->bornTicks == 200, "replacement retained the wrong creation time");
        Require(!observations.course, "replacement retained an old movie course");
    }

    void TestCurrentReplacementCourseSurvivesDrain()
    {
        HudObservationInbox inbox;
        inbox.RecordMovieCreated(100);
        inbox.Drain();
        inbox.RecordMovieCreated(200);
        inbox.RecordCourse(2, 0x5678);

        const auto observations = inbox.Drain();
        Require(observations.movie.has_value(), "replacement movie was not recorded");
        Require(observations.course.has_value(), "replacement course was not recorded");
        Require(observations.course->generation == observations.movie->generation, "movie and course generations disagreed");
        Require(observations.course->courseId == 0x5678, "replacement course retained the wrong body");
    }
}

void RunHudObservationInboxTests()
{
    TestMoviePublishesOneCoherentObservation();
    TestLatestCourseReplacesStaleValue();
    TestReplacementRejectsOldGenerationBeforeDrain();
    TestCurrentReplacementCourseSurvivesDrain();
}
