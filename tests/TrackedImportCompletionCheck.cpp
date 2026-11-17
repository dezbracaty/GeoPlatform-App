#include <ActionManager.hpp>
#include <ActionHandlerRegistry.hpp>
#include <ActionContext.hpp>
#include <IActionHandlerBase.hpp>
#include <Foundation/Log.h>
#include <QCoreApplication>
#include <QEventLoop>
#include <QTimer>
#include <iostream>
#include <stdexcept>

class FiniteImport final : public IActionHandlerBase {
public:
    int exits{0};
    bool cleanupReentryRejected{true};
    std::function<void()> cleaned;
    std::shared_ptr<ActionContext> current;
    HandlerType getHandlerType() const override { return HandlerType::Exclusive; }
    void onEnter(std::shared_ptr<ActionContext> context) override {
        current = context;
        if (context->getParams().value("immediate").toBool()) {
            context->setResult(QVariantMap{{"success", true}});
            completeInvocation(context);
            setActive(false);
            return;
        }
        if (context->getParams().value("syncFail").toBool()) {
            context->setError(ActionErrorCode::InvalidParams, "unreadable");
            setActive(false);
            return;
        }
        setActive(true);
        emit requestBackgroundExecution();
    }
    void onSuspend() override { setActive(false); }
    void onResume() override {
        setActive(true);
        const auto context = current;
        const auto complete = [this, context] {
            context->setResult(QVariantMap{{"success", !context->getParams().value("cancel").toBool() && !context->getParams().value("failure").toBool()},
                {"cancelled", context->getParams().value("cancel")}, {"diagnostics", "preserve me"}});
            completeInvocation(context);
            setActive(false);
        };
        if (context->getParams().value("inlineFinish").toBool()) complete();
        else QTimer::singleShot(0, this, complete);
    }
    void finish() { emit requestResume(); }
    void lateFinish(const std::shared_ptr<ActionContext>& context) { completeInvocation(context); }
    void onExit() override {
        ++exits;
        cleanupReentryRejected &= !ActionManager::getInstance()->invokeTrackedAction("test.native.import").value("accepted").toBool();
        current.reset(); setActive(false);
        if (cleaned) cleaned();
    }
};

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    Log::setup(argv, "TrackedImportCompletionCheck");
    const auto require = [](bool ok, const char* message) { if (!ok) throw std::runtime_error(message); };
    try {
        auto handler = std::make_shared<FiniteImport>();
        ActionHandlerRegistry::instance().registerFactory("test.native.import", [handler] { return handler; });
        auto* manager = ActionManager::getInstance();
        const auto failed = manager->invokeTrackedAction("test.native.import", {{"syncFail", true}});
        require(!failed.value("pending").toBool() && !failed.value("success").toBool(), "sync failure pending");
        require(handler->exits == 1, "sync failure did not clean exactly once");
        auto first = manager->invokeTrackedAction("test.native.import", {{"cancel", true}});
        auto oldContext = handler->current;
        require(first.value("pending").toBool() && !first.value("isAIInvoke").toBool(), "native call not pending");
        auto blocked = manager->invokeTrackedAction("test.native.import");
        require(!blocked.value("accepted").toBool(), "reentrant importer accepted");
        QEventLoop loop;
        int completions = 0;
        QString secondId;
        const auto connection = QObject::connect(manager, &ActionManager::invocationCompleted, &loop,
            [&](const QString& id, const QVariantMap& outcome) {
                ++completions;
                if (id == first.value("invocationId").toString()) {
                    require(handler->exits == 2 && !handler->current, "completion preceded cleanup");
                    require(outcome.value("status") == "cancelled" && !outcome.value("success").toBool(), "cancel reported success");
                    require(outcome.value("result").toMap().value("diagnostics") == "preserve me", "cleanup erased outcome");
                    auto second = manager->invokeTrackedAction("test.native.import", {{"failure", true}});
                    require(second.value("pending").toBool(), "next import cannot enter released slot");
                    secondId = second.value("invocationId").toString();
                    handler->lateFinish(oldContext);
                    handler->finish();
                } else {
                    require(id == secondId && !outcome.value("success").toBool() && outcome.value("status") == "failed", "business failure reported success");
                    require(handler->exits == 3, "second completion did not clean exactly once");
                    loop.quit();
                }
            });
        handler->finish();
        QTimer::singleShot(3000, &loop, &QEventLoop::quit);
        loop.exec();
        require(completions == 2, "missing or duplicate completion");
        require(handler->cleanupReentryRejected, "onExit allowed reentrant import before cleanup finished");
        const auto immediate = manager->invokeTrackedAction("test.native.import", {{"immediate", true}});
        require(!immediate.value("pending").toBool() && immediate.value("success").toBool(), "immediate completion left pending");
        QCoreApplication::processEvents();
        require(completions == 2 && handler->exits == 4, "immediate or stale callback cleaned twice");
        QObject::disconnect(connection);

        // The old completion is queued, then the registry's same instance is
        // reused by an untracked native caller before that callback arrives.
        const auto old = manager->invokeTrackedAction("test.native.import", {{"inlineFinish", true}});
        handler->finish();
        manager->deactivateActiveHandler();
        const auto legacy = manager->invokeActionWithReceipt("test.native.import");
        auto newerContext = handler->current;
        require(legacy.value("pending").toBool(), "cannot set up registry reuse");
        QEventLoop oldCallback;
        int oldCompletions = 0;
        const auto late = QObject::connect(manager, &ActionManager::invocationCompleted, &oldCallback,
            [&](const QString& id, const QVariantMap&) {
                if (id == old.value("invocationId").toString()) { ++oldCompletions; oldCallback.quit(); }
            });
        QTimer::singleShot(3000, &oldCallback, &QEventLoop::quit);
        oldCallback.exec();
        require(oldCompletions == 1 && handler->current == newerContext && handler->exits == 4,
                "old callback cleaned the registry's newer context");
        QObject::disconnect(late);
        QEventLoop cleanup;
        handler->cleaned = [&] { cleanup.quit(); };
        handler->finish();
        QTimer::singleShot(3000, &cleanup, &QEventLoop::quit);
        cleanup.exec();
        handler->cleaned = {};
        require(handler->exits == 5, "legacy cleanup contract changed");
        manager->cleanupBeforeShutdown();
        std::cout << "PASS native completion identity, rejection, cancellation, cleanup and immediate next import\n";
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
    Log::shutdown();
}
