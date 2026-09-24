#pragma once

#include <AutoRegisterDB.hpp>

/**
 * Temporary cut-preview presentation state.
 *
 * The referenced CutPlaneWidgetDB remains the sole owner of PlaneToWorld.
 * Revision is only an explicit invalidation token for this derived render
 * projection; it does not duplicate the plane pose.
 */
class CutPreviewDB final : public AutoRegisterDB {
public:
    CutPreviewDB() = default;
    ~CutPreviewDB() override = default;

    void initializeProperties() override;

    TypeID getTypeID() const override { return TypeID::CUT_PREVIEW_DB; }

    FIELD_VALUE(CutPreviewDB, DBInstanceID, SourceModelID)
    FIELD_VALUE(CutPreviewDB, DBInstanceID, PlaneWidgetID)
    FIELD_VALUE_SIMPLE(CutPreviewDB, bool, Enabled)
    FIELD_VALUE_SIMPLE(CutPreviewDB, int, Revision)
};
