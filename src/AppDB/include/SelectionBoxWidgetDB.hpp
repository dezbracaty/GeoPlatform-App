#pragma once

#include "WidgetDB.hpp"
#include <SystemTypes.hpp>

/**
 * @brief 选中模型的瞬态角点包围框数据。
 *
 * 每个被选中的 Actor 对应一个 Widget，由 DB Relation 跟随 Actor 变换。
 */
class SelectionBoxWidgetDB : public WidgetDB {
public:
    SelectionBoxWidgetDB();
    explicit SelectionBoxWidgetDB(const DBInstanceID& linkedActorId);
    ~SelectionBoxWidgetDB() override = default;

    void initializeProperties() override;
    void initializeSubWidgetProperties() override;

    TypeID getTypeID() const override {
        return TypeID::SELECTION_BOX_WIDGET_DB;
    }

    FIELD_RELATION_REF(SelectionBoxWidgetDB, LinkedActorID)
    FIELD_VALUE(SelectionBoxWidgetDB, Vector3, ModelBoundsMin)
    FIELD_VALUE(SelectionBoxWidgetDB, Vector3, ModelBoundsMax)
    FIELD_VALUE_SIMPLE(SelectionBoxWidgetDB, bool, HasValidBounds)

    void updateFromLinkedActor();
    void setLinkedActor(const DBInstanceID& actorId);

protected:
    void onFullyInitialized() override;

private:
    DBInstanceID m_initialLinkedActorId;
    bool m_hasInitialLinkedActor = false;
};
