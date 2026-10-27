#include <ActionManager.hpp>
#include <ActionHandlerRegistry.hpp>
#include <ActionContext.hpp>
#include <IActionHandlerBase.hpp>
#include <Foundation/Log.h>
#include <QCoreApplication>
#include <iostream>
#include <stdexcept>

class Probe final : public IActionHandlerBase {
public:
    HandlerType getHandlerType() const override { return HandlerType::Exclusive; }
    void onEnter(std::shared_ptr<ActionContext> context) override {
        if (context->getActionCode() == "test.throw") throw std::runtime_error("expected failure");
        context->setResult(context->getActionCode());
        setActive(false);
    }
    void onExit() override { setActive(false); }
};

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    Log::setup(argv, "StartupAdmissionCheck");
    auto* manager = ActionManager::getInstance();
    int constructed = 0;
    auto& registry = ActionHandlerRegistry::instance();
    for (const auto* name : {"test.outer", "test.inner", "test.throw"})
        registry.registerFactory(QString::fromLatin1(name), [&] {
            ++constructed;
            return std::make_shared<Probe>();
        });
    const auto require = [](bool condition, const char* message) {
        if (!condition) throw std::runtime_error(message);
    };
    try {
        manager->setAdmissionPolicy([](const QString&, bool) { return QString("Starting"); });
        require(!manager->triggerAction("test.outer"), "native action escaped admission");
        require(!manager->triggerActionByAI("test.outer"), "AI action escaped admission");
        require(!manager->invokeActionByAI("test.outer").value("accepted").toBool(), "AI receipt accepted");
        const auto deniedSequence = manager->triggerActionSequenceByAI(
            {QVariantMap{{"actionCode", "test.outer"}}}, {{"invocationId", "blocked-sequence"}});
        require(deniedSequence.value("invocationId").toString() == "blocked-sequence", "AI sequence rejection identity lost");
        manager->listActionsForAI(); manager->listActionSummariesForAI();
        manager->describeActionForAI("test.outer");
        const auto denied = manager->invokeActionWithReceipt("test.outer", {}, {{"startupEpoch", 7}});
        require(!denied.value("accepted").toBool() && !denied.value("pending").toBool(), "bad rejection receipt");
        require(denied.value("metadata").toMap().value("startupEpoch").toInt() == 7, "rejection identity lost");
        require(constructed == 0, "admission constructed a handler");
        manager->setAdmissionPolicy({});
        QObject::connect(manager, &ActionManager::actionCompleted, manager,
            [manager](const QString& code, bool) {
                if (code == "test.outer") manager->triggerAction("test.inner");
            });
        const auto outer = manager->invokeActionWithReceipt("test.outer", {}, {{"startupEpoch", 8}});
        require(outer.value("success").toBool() && outer.value("result").toString() == "test.outer", "reentrant signal overwrote receipt");
        require(outer.value("actionCode").toString() == "test.outer", "wrong invocation returned");
        const auto thrown = manager->invokeActionWithReceipt("test.throw");
        require(!thrown.value("success").toBool() && thrown.value("error").toString().contains("expected failure"), "exception receipt lost");
        manager->stopAcceptingActions();
        const auto shutdown = manager->invokeActionWithReceipt("test.outer");
        require(!shutdown.value("accepted").toBool(), "shutdown accepted action");
        std::cout << "PASS startup admission before construction, AI/capability paths, reentrant receipts and exception/shutdown results\n";
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
    Log::shutdown();
    return 0;
}
