#include "CutPreviewDB.hpp"

void CutPreviewDB::initializeProperties() {
    setSourceModelID(INVALID_DB_ID);
    setPlaneWidgetID(INVALID_DB_ID);
    setEnabled(true);
    setRevision(0);
    setDisplayName("Cut Preview");
}
