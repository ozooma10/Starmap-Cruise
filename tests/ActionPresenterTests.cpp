#include "Presentation/ActionPresenter.h"
#include "TestSuites.h"

#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace
{
    using CFS::Localization::Language;
    using CFS::Localization::TextId;

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
            .labelId = TextId::SetCruiseTarget,
            .holdLabelId = TextId::HoldToCruise,
            .destination = ::Destination {
                .kind = ::DestinationKind::Planet,
                .targetId = 0x10,
                .courseId = 0x10,
                .system = {.starFormId = 0x1000, .numericId = 0x100},
                .displayName = "Jemison",
            },
        };
    }

    void TestFirstPresentationIsApplied()
    {
        ::ActionPresenter presenter;
        FakeMapActionView view;

        const auto result = presenter.Present(ReadyDecision(), Language::English, view);

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

        presenter.Present(decision, Language::English, view);
        const auto repeated = presenter.Present(decision, Language::English, view);

        Require(!repeated.changed, "identical presentation was reported as changed");
        Require(repeated.applied, "cached presentation was not reported as reflected by the view");
        Require(view.attempts.size() == 1, "identical presentation touched the view again");
    }

    void TestNonRenderedChangesAreSuppressed()
    {
        ::ActionPresenter presenter;
        FakeMapActionView view;
        presenter.Present(ReadyDecision(), Language::English, view);

        auto changedDecision = ReadyDecision();
        changedDecision.selectionReason = ::SelectionReason::TargetDataUpdating;
        changedDecision.destination->targetId = 0x20;
        changedDecision.destination->courseId = 0x20;
        changedDecision.destination->displayName = "Mars";

        const auto result = presenter.Present(changedDecision, Language::English, view);

        Require(!result.changed, "non-rendered decision details invalidated the presentation");
        Require(result.applied, "unchanged rendered model was not reported as applied");
        Require(view.attempts.size() == 1, "non-rendered decision details touched the view");
    }

    void TestRenderedChangeIsApplied()
    {
        ::ActionPresenter presenter;
        FakeMapActionView view;
        presenter.Present(ReadyDecision(), Language::English, view);

        auto changedDecision = ReadyDecision();
        changedDecision.control = ::ActionControl::TapOnly;
        changedDecision.enabled = false;
        changedDecision.labelId = TextId::SetCruiseTarget;
        changedDecision.holdLabelId = TextId::None;

        const auto result = presenter.Present(changedDecision, Language::English, view);

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
        presenter.Present(ReadyDecision(), Language::English, view);

        const auto result = presenter.Present(::ActionDecision {}, Language::English, view);

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

        const auto failed = presenter.Present(ReadyDecision(), Language::English, view);

        Require(failed.changed && !failed.applied, "failed view write produced the wrong result");
        Require(view.attempts.size() == 1, "failed view write was not attempted exactly once");

        view.applySucceeds = true;
        const auto retried = presenter.Present(ReadyDecision(), Language::English, view);

        Require(retried.changed && retried.applied, "failed presentation was not retried");
        Require(view.attempts.size() == 2, "presentation was cached despite the failed write");

        const auto cached = presenter.Present(ReadyDecision(), Language::English, view);
        Require(!cached.changed && cached.applied, "successful retry was not cached");
        Require(view.attempts.size() == 2, "cached retry touched the view again");
    }

    void TestInvalidationForcesReapplication()
    {
        ::ActionPresenter presenter;
        FakeMapActionView view;
        presenter.Present(ReadyDecision(), Language::English, view);

        presenter.Invalidate();

        const auto result = presenter.Present(ReadyDecision(), Language::English, view);

        Require(result.changed && result.applied, "invalidation did not force reapplication");
        Require(view.attempts.size() == 2, "invalidated presentation did not touch the view again");
    }

    void TestFailedReplacementPreservesLastAppliedPresentation()
    {
        ::ActionPresenter presenter;
        FakeMapActionView view;
        const auto original = ReadyDecision();
        Require(presenter.Present(original, Language::English, view).applied, "original presentation setup failed");

        auto replacement = original;
        replacement.control = ::ActionControl::TapOnly;
        replacement.enabled = false;
        replacement.labelId = TextId::JumpThenCruise;
        replacement.holdLabelId = TextId::None;

        view.applySucceeds = false;
        const auto failed = presenter.Present(replacement, Language::English, view);
        Require(failed.changed && !failed.applied, "failed replacement produced the wrong result");
        Require(view.attempts.size() == 2, "failed replacement was not attempted once");

        view.applySucceeds = true;
        const auto originalAgain = presenter.Present(original, Language::English, view);
        Require(!originalAgain.changed && originalAgain.applied, "failed replacement corrupted the last applied cache");
        Require(view.attempts.size() == 2, "already-rendered original was written after a failed replacement");

        const auto retried = presenter.Present(replacement, Language::English, view);
        Require(retried.changed && retried.applied, "replacement was not retryable");
        Require(view.attempts.size() == 3, "replacement retry touched the view the wrong number of times");
    }

    void TestTranslatedPresentationAndLanguageChange()
    {
        ::ActionPresenter presenter;
        FakeMapActionView view;
        const auto decision = ReadyDecision();

        const auto first = presenter.Present(decision, Language::German, view);
        Require(first.changed && first.applied, "first localized presentation was not applied");
        Require(view.attempts.front().label == "ZIEL FÜR REISEMODUS FESTLEGEN", "first tap label was not localized");
        Require(view.attempts.front().holdLabel == "FÜR REISEMODUS GEDRÜCKT HALTEN", "first hold label was not localized");

        const auto repeated = presenter.Present(decision, Language::German, view);
        Require(!repeated.changed && repeated.applied, "localized presentation was not cached");
        Require(view.attempts.size() == 1, "unchanged localized presentation touched the view");

        view.applySucceeds = false;
        const auto failed = presenter.Present(decision, Language::Japanese, view);
        Require(failed.changed && !failed.applied, "failed language change was reported as applied");
        Require(view.attempts.back().label == "クルーズの目的地を設定", "language change lost the Japanese tap text");
        Require(view.attempts.back().holdLabel == "長押しでクルーズを開始", "language change lost the Japanese hold text");

        view.applySucceeds = true;
        const auto oldLanguage = presenter.Present(decision, Language::German, view);
        Require(!oldLanguage.changed && oldLanguage.applied, "failed language change corrupted the cache");
        Require(view.attempts.size() == 2, "failed language change replaced the last applied language");

        const auto retried = presenter.Present(decision, Language::Japanese, view);
        Require(retried.changed && retried.applied, "language change was not retried");
        const auto cached = presenter.Present(decision, Language::Japanese, view);
        Require(!cached.changed && cached.applied, "successful language change was not cached");
        Require(view.attempts.size() == 3, "language change had unexpected view writes");
    }

    void TestTranslatedRemoteAndSuppressedLabels()
    {
        ::ActionPresenter presenter;
        FakeMapActionView view;
        auto decision = ReadyDecision();
        decision.control = ::ActionControl::TapOnly;
        decision.labelId = TextId::JumpThenCruise;
        decision.holdLabelId = TextId::None;
        decision.requiresTravel = true;

        Require(presenter.Present(decision, Language::SimplifiedChinese, view).applied, "remote presentation failed");
        Require(view.attempts.back().label == "跃迁后巡航", "remote label was not localized");
        Require(view.attempts.back().holdLabel.empty(), "remote action retained a hold label");

        // Even stale IDs must not leak text into a hidden or absent hold prompt.
        decision.holdLabelId = TextId::HoldToCruise;
        Require(!presenter.Present(decision, Language::SimplifiedChinese, view).changed, "tap-only hold ID changed rendered text");
        decision.control = ::ActionControl::Hidden;
        decision.enabled = false;
        Require(presenter.Present(decision, Language::SimplifiedChinese, view).applied, "hidden presentation failed");
        Require(view.attempts.back().label.empty() && view.attempts.back().holdLabel.empty(), "hidden action retained translated text");
        Require(!presenter.Present(decision, Language::German, view).changed, "language change rewrote an empty hidden presentation");

        decision = ReadyDecision();
        decision.enabled = false;
        Require(presenter.Present(decision, Language::French, view).applied, "disabled combo presentation failed");
        Require(!view.attempts.back().label.empty(), "disabled visible action lost its label");
        Require(view.attempts.back().holdLabel.empty(), "disabled combo exposed a hold label");

        decision = ReadyDecision();
        decision.holdLabelId = TextId::None;
        Require(presenter.Present(decision, Language::French, view).applied, "absent hold presentation failed");
        Require(view.attempts.back().holdLabel.empty(), "absent hold ID produced text");
    }

    void RunTests()
    {
        TestTranslatedPresentationAndLanguageChange();
        TestTranslatedRemoteAndSuppressedLabels();
        TestFirstPresentationIsApplied();
        TestIdenticalPresentationIsSuppressed();
        TestNonRenderedChangesAreSuppressed();
        TestRenderedChangeIsApplied();
        TestHiddenDecisionIsApplied();
        TestFailedApplicationIsRetried();
        TestInvalidationForcesReapplication();
        TestFailedReplacementPreservesLastAppliedPresentation();
    }
}

void RunActionPresenterTests()
{
    RunTests();
}
