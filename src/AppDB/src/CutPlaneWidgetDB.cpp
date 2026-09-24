#include "CutPlaneWidgetDB.hpp"

void CutPlaneWidgetDB::initializeProperties() {
    WidgetDB::initializeProperties();
}

void CutPlaneWidgetDB::initializeSubWidgetProperties() {
    setTargetModelID(INVALID_DB_ID);
    setPlaneToWorld(Transform());
    setWidgetRadius(10.0f);
    setActivePart(static_cast<int>(Part::None));
    setIsDragging(false);

    setName("Cut Plane Widget");
    setVisible(true);
    setInteractive(true);
    setOpacity(1.0f);
    setPriority(2.0f);
    setRenderLayer(2);
}
