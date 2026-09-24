#include "../include/Transform.hpp"
#include <rttr/registration>

// RTTR 注册 Transform 类型
RTTR_REGISTRATION
{
    using namespace rttr;

    // 只注册类型，不注册属性，因为 Eigen 矩阵可能不兼容
    registration::class_<Transform>("Transform")
        .constructor<>()
        .constructor<const Transform&>();
}
