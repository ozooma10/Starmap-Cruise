#include "Presentation/ActionPresenter.h"
#include "TestSuites.h"

#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace
{
    class FakeMapActionView final : public ::MapActionView
    {
    public:
        bool Apply(const ::MapActionPresentation& presentation) override
        {
            attempts.push_back(presentation);
            return applySucceeds;
        }

        bool applySucceeds {true};
        std::vector<::MapActionPresentation> attempts;
    };

    void Require(bool condition, std::string_view message)
    {
        if (!condition)
            throw std::runtime_error {std::string {message}};
    }

    ::ActionDecision ReadyDecision()
    {
        return {
            .control = ::ActionControl::TapAndHold,
            .enabled = true,
            .selectionReason = ::SelectionReason::Eligible,
            .label = "SET CRUISE TARGET",
            .holdLabel = "HOLD TO CRUISE",
            .destination = ::Destination {
                .kind = ::DestinationKind::Planet,
                .targetId = 0x10,
                .courseId = 0x10,
                .systemId = 0x100,
                .displayName = "Jemison",
            },
        };
    }

    void TestFirstPresentationIsApplied()
    {
        ::ActionPresenter presenter;
        FakeMapActionView view;

        const auto result = presenter.Present(ReadyDecision(), view);

        Require(result.changed, "first presentation was not reported as changed");
        Require(result.applied, "first presentation was not reported as applied");
        Require(view.attempts.size() == 1, "first presentation was not applied exactly once");

        const auto& presentation = view.attempts.front();
        Require(presentation.control == ::ActionControl::TapAndHold, "presentation retained the wrong control type");
        Require(presentation.enabled, "presentation lost the enabled state");
        Require(presentation.label == "SET CRUISE TARGET", "presentation retained the wrong tap label");
        Require(presentation.holdLabel == "HOLD TO CRUISE", "presentation retained the wrong hold label");
    }

    void TestIdenticalPresentationIsSuppressed()
    {
        ::ActionPresenter presenter;
        FakeMapActionView view;
        const auto decision = ReadyDecision();

        presenter.Present(decision, view);
        const auto repeated = presenter.Present(decision, view);

        Require(!repeated.changed, "identical presentation was reported as changed");
        Require(repeated.applied, "cached presentation was not reported as reflected by the view");
        Require(view.attempts.size() == 1, "identical presentation touched the view again");
    }

    void TestNonRenderedChangesAreSuppressed()
    {
        ::ActionPresenter presenter;
        FakeMapActionView view;
        presenter.Present(ReadyDecision(), view);

        auto changedDecision = ReadyDecision();
        changedDecision.selectionReason = ::SelectionReason::TargetDataUpdating;
        changedDecision.destination->targetId = 0x20;
        changedDecision.destination->courseId = 0x20;
        changedDecision.destination->displayName = "Mars";

        const auto result = presenter.Present(changedDecision, view);

        Require(!result.changed, "non-rendered decision details invalidated the presentation");
        Require(result.applied, "unchanged rendered model was not reported as applied");
        Require(view.attempts.size() == 1, "non-rendered decision details touched the view");
    }

    void TestRenderedChangeIsApplied()
    {
        ::ActionPresenter presenter;
        FakeMapActionView view;
        presenter.Present(ReadyDecision(), view);

        auto changedDecision = ReadyDecision();
        changedDecision.control = ::ActionControl::TapOnly;
        changedDecision.enabled = false;
        changedDecision.label = "CRUISE UNAVAILABLE";
        changedDecision.holdLabel.clear();

        const auto result = presenter.Present(changedDecision, view);

        Require(result.changed && result.applied, "rendered change was not applied");
        Require(view.attempts.size() == 2, "rendered change did not touch the view exactly once");
        Require(view.attempts.back().control == ::ActionControl::TapOnly, "changed presentation retained the old control type");
        Require(!view.attempts.back().enabled, "changed presentation retained the old enabled state");
        Require(view.attempts.back().holdLabel.empty(), "tap-only presentation retained the hold label");
    }

    void TestHiddenDecisionIsApplied()
    {
        ::ActionPresenter presenter;
        FakeMapActionView view;
        presenter.Present(ReadyDecision(), view);

        const auto result = presenter.Present(::ActionDecision {}, view);

        Require(result.changed && result.applied, "hidden decision was not applied");
        Require(view.attempts.size() == 2, "hidden decision did not update the visible view");
        Require(view.attempts.back().control == ::ActionControl::Hidden, "hidden decision retained a visible control");
        Require(!view.attempts.back().enabled, "hidden decision retained the enabled state");
    }

    void TestFailedApplicationIsRetried()
    {
        ::ActionPresenter presenter;
        FakeMapActionView view;
        view.applySucceeds = false;

        const auto failed = presenter.Present(ReadyDecision(), view);

        Require(failed.changed && !failed.applied, "failed view write produced the wrong result");
        Require(view.attempts.size() == 1, "failed view write was not attempted exactly once");

        view.applySucceeds = true;
        const auto retried = presenter.Present(ReadyDecision(), view);

        Require(retried.changed && retried.applied, "failed presentation was not retried");
        Require(view.attempts.size() == 2, "presentation was cached despite the failed write");

        const auto cached = presenter.Present(ReadyDecision(), view);
        Require(!cached.changed && cached.applied, "successful retry was not cached");
        Require(view.attempts.size() == 2, "cached retry touched the view again");
    }

    void TestInvalidationForcesReapplication()
    {
        ::ActionPresenter presenter;
        FakeMapActionView view;
        presenter.Present(ReadyDecision(), view);

        presenter.Invalidate();

        const auto result = presenter.Present(ReadyDecision(), view);

        Require(result.changed && result.applied, "invalidation did not force reapplication");
        Require(view.attempts.size() == 2, "invalidated presentation did not touch the view again");
    }

    void RunTests()
    {
        TestFirstPresentationIsApplied();
        TestIdenticalPresentationIsSuppressed();
        TestNonRenderedChangesAreSuppressed();
        TestRenderedChangeIsApplied();
        TestHiddenDecisionIsApplied();
        TestFailedApplicationIsRetried();
        TestInvalidationForcesReapplication();
    }
}

void RunActionPresenterTests()
{
    RunTests();
}
