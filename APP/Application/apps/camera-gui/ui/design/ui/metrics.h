#pragma once

#include <QtGlobal>

namespace Ui
{
namespace Metrics
{
// 页面使用设计坐标，Dashboard 统一等比缩放到实际窗口尺寸。
constexpr int DefaultWidth = 800;
constexpr int DefaultHeight = 480;
constexpr qreal DesignWidth = 1600;
constexpr qreal DesignHeight = 960;
constexpr qreal HeaderHeight = 98;
constexpr qreal NavigationHeight = 78;
constexpr qreal NavigationDragHeight = 71;
} // namespace Metrics
} // namespace Ui
