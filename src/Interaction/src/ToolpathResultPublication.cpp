#include "ToolpathResultPublication.hpp"
#include <SlicingPreviewBridge.hpp>
#include <DocumentManager.hpp>
#include <PrintBedDB.hpp>
#include <PrintBedPlacementUtil.hpp>
#include <ToolpathPreviewRelations.hpp>
#include <TransactionManager.hpp>
#include <ImmediateNotifyGuard.hpp>
#include <stdexcept>

namespace GPlatform {
ToolpathPublicationResult publishToolpathResult(std::shared_ptr<SliceOutputRecord> output,
    PreparedToolpathPreview prepared, std::uint64_t epoch,
    const std::function<void(PreviewPublicationStage)>& beforeOperation) {
    ToolpathPublicationResult result;
    auto* bridge = SlicingPreviewBridge::instance();
    if (!output || prepared.cancelled || !bridge->acceptsPublication(epoch)) {
        result.error = QStringLiteral("Task cancelled or superseded"); return result;
    }
    const bool modelSlice = output->sourceScope == ToolpathPreviewSourceScope::SingleModel ||
        output->sourceScope == ToolpathPreviewSourceScope::Plate;
    if (modelSlice && !ToolpathPreviewRelations::sourceModelsAvailable(output->sourceModelIds, output->printBedId)) {
        result.error = QStringLiteral("Slice source models are missing or no longer belong to the print bed");
        return result;
    }
    if (!output->validate(&result.error)) return result;
    output->previewError = prepared.error;
    output->warnings.append(prepared.warnings);
    if (prepared.geometryAvailable) output->stats = prepared.data.stats;
    auto* document = DocumentManager::instance();
    // Pin both sides until rollback notifications are drained or the committed
    // result is installed. Document's pending changes contain raw DB pointers.
    const auto previousOutput = bridge->currentOutput();
    const auto previousPreview = bridge->currentToolpathPreview();
    const auto previousMaterial = previousPreview ? previousPreview->getMaterial() : nullptr;
    const auto before = [&](auto stage) { if (beforeOperation) beforeOperation(stage); };
    enum class Attempt { Accepted, PreviewFailed, Rejected };
    const auto attempt = [&](bool withPreview) {
        std::shared_ptr<ToolpathPreviewDB> candidate;
        std::shared_ptr<MaterialDB> candidateMaterial;
        std::unique_ptr<TransactionGuard> transaction;
        std::optional<SlicingPreviewBridge::PreparedOutputPublication> publication;
        bool mayDegrade = withPreview;
        try {
            const bool previousRegistered = previousPreview && document->getDBInstance(previousPreview->getDBInstanceID());
            if (withPreview || previousRegistered)
                transaction = std::make_unique<TransactionGuard>("Publish toolpath preview");
            if (withPreview) {
                before(PreviewPublicationStage::Create);
                candidate = trans::TransDB::create<ToolpathPreviewDB>();
                if (!candidate) throw std::runtime_error("Failed to create toolpath preview");
                candidateMaterial = candidate->getMaterial();
                auto& preview = candidate;
                auto data = std::move(prepared.data);
                const bool beltAudit = data.metadataJson.find("gplatform.belt_support_algorithm_audit.v1") != std::string::npos;
                data.printGCodePath = output->gcode.path.toStdString();
                data.printGCode3mfPath = output->package.path.toStdString();
                data.printGCodeOwnership = data.printGCode3mfOwnership = ToolpathArtifactOwnership::External;
                preview->setSourceModelName(output->name.toStdString());
                preview->setSourceScope(output->sourceScope);
                preview->setSourceModelDBId(output->sourceModelId);
                preview->setPreviewData(std::move(data));
                const auto bed = document->getDB<PrintBedDB>(output->printBedId);
                if (!bed) throw std::runtime_error("The preview print bed is unavailable");
                // Toolpath coordinates stay plate-local, including exported G-code.
                // Ownership does not compose transforms in the renderer.
                Transform displayTransform;
                displayTransform.setPosition(PrintBedPlacementUtil::sceneOffset(*bed));
                preview->setTransform(displayTransform);
                preview->setVisible(true);
                if (beltAudit) {
                    preview->setShowLayerRangeStart(0); preview->setShowLayerRangeEnd(0); preview->setCurrentLayer(0);
                }
                before(PreviewPublicationStage::Register);
                if (!document || !document->registerDBInstance(preview, TypeID::TOOLPATH_PREVIEW_DB).isValid())
                    throw std::runtime_error("Failed to register toolpath preview");
                before(PreviewPublicationStage::Attach);
                if (!output->printBedId.isValid() || !document->attachOwnedChild(output->printBedId,
                        preview->getDBInstanceID(), ToolpathPreviewRelations::PrintBedPreviewsRelation))
                    throw std::runtime_error("Failed to attach toolpath preview to print bed");
                if (output->sourceScope == ToolpathPreviewSourceScope::ImportedGCode ||
                    output->sourceScope == ToolpathPreviewSourceScope::Calibration)
                    output->displayModelId = static_cast<int>(preview->getDBInstanceID().getValue());
            }
            mayDegrade = false;
            if (modelSlice && candidate) {
                ImmediateNotifyGuard immediate;
                candidate->setSourceModelDBIds(output->sourceModelIds);
                if (!ToolpathPreviewRelations::sourceEdgesMatch(candidate->getDBInstanceID(), output->sourceModelIds))
                    throw std::runtime_error("Failed to bind all slice source models");
            }
            publication = bridge->prepareOutputPublication(output, candidate, epoch, &result.error);
            if (!publication) throw std::runtime_error(result.error.toStdString());
            before(PreviewPublicationStage::RemovePrevious);
            if (previousRegistered && previousPreview != candidate &&
                !document->unregisterDBInstance(previousPreview->getDBInstanceID()))
                throw std::runtime_error("Failed to remove previous toolpath preview");
            before(PreviewPublicationStage::AfterRemovePrevious);
            if (transaction) {
                mayDegrade = withPreview; // Only the pre-commit test hook can downgrade here.
                before(PreviewPublicationStage::Commit);
                mayDegrade = false;
            }
            if (!bridge->acceptsPublication(epoch)) throw std::runtime_error("Task cancelled or superseded");
            if (!output->validate(&result.error)) throw std::runtime_error(result.error.toStdString());
            if (modelSlice && (!ToolpathPreviewRelations::sourceModelsAvailable(output->sourceModelIds, output->printBedId) ||
                (candidate && !ToolpathPreviewRelations::sourceEdgesMatch(candidate->getDBInstanceID(), output->sourceModelIds))))
                throw std::runtime_error("Slice source models or dependencies changed before publication");
            if (transaction) transaction->commit();
        } catch (const std::bad_alloc&) {
            if (transaction) { transaction->rollback(); document->flushPendingChanges(); }
            throw;
        }
        catch (const std::exception& error) {
            if (transaction) { transaction->rollback(); document->flushPendingChanges(); }
            result.error = QString::fromUtf8(error.what());
            if (mayDegrade && bridge->acceptsPublication(epoch) && SliceOutputRecord::readable(output->gcode.path)) {
                output->previewError = result.error;
                return Attempt::PreviewFailed;
            }
            return Attempt::Rejected;
        } catch (...) {
            if (transaction) { transaction->rollback(); document->flushPendingChanges(); }
            throw;
        }
        // No rejectable work or failure hooks remain after the commit.
        bridge->installCommittedOutput(std::move(*publication));
        result.preview = std::move(candidate);
        result.accepted = true;
        result.error.clear();
        return Attempt::Accepted;
    };
    if (prepared.geometryAvailable) {
        const auto outcome = attempt(true);
        if (outcome != Attempt::PreviewFailed) return result;
    }
    if (output->sourceScope == ToolpathPreviewSourceScope::ImportedGCode) {
        result.error = output->previewError.isEmpty() ? QStringLiteral("G-code has no compatible preview") : output->previewError;
        return result;
    }
    if (output->previewError.isEmpty()) output->previewError = QStringLiteral("No toolpath preview was provided");
    attempt(false);
    return result;
}
}
