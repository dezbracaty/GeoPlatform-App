#pragma once
#include "ActorDB.hpp"
#include <memory>
#include <vector>

// Transient presentation data for one view. This is the result of an interaction,
// not a source of snap candidates. No renderer objects cross this boundary.
class SnapPreviewDB final : public ActorDB {
public:
    struct Line {
        Vector3 start, end;
    };
    struct Preview {
        DBInstanceID ownerViewId;
        std::vector<Line> lines;
        BoundingBox bounds;
    };
    using PreviewPtr = std::shared_ptr<const Preview>;

    TypeID getTypeID() const override { return TypeID::SNAP_PREVIEW_DB; }
    void setPreview(DBInstanceID ownerViewId, std::vector<Line> lines);
    PreviewPtr preview() const;
    DBInstanceID ownerViewId() const;
    BoundingBox localBounds() const override;
    std::shared_ptr<AutoRegisterDB> clone() const override;

protected:
    void initializeSubActorProperties() override;
    void onFullyInitialized() override;

private:
    // Readers retain an immutable snapshot while the owner publishes the next.
    PreviewPtr m_preview;
};
