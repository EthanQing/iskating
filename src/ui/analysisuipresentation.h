#ifndef ANALYSISUIPRESENTATION_H
#define ANALYSISUIPRESENTATION_H

#include "trainingdomain.h"

#include <algorithm>

namespace AnalysisUiPresentation {

inline QString taskType(const QString &type)
{
    if (type == QStringLiteral("offline_import")) return QStringLiteral("离线视频导入");
    if (type == QStringLiteral("full_rate_batch")) return QStringLiteral("完整帧率分析");
    return type;
}

inline QString status(const QString &value)
{
    if (value == QStringLiteral("queued")) return QStringLiteral("排队中");
    if (value == QStringLiteral("running")) return QStringLiteral("运行中");
    if (value == QStringLiteral("paused")) return QStringLiteral("已暂停");
    if (value == QStringLiteral("partial")) return QStringLiteral("部分完成");
    if (value == QStringLiteral("completed")) return QStringLiteral("已完成");
    if (value == QStringLiteral("failed")) return QStringLiteral("失败");
    if (value == QStringLiteral("cancelled")) return QStringLiteral("已取消");
    return value;
}

inline double taskProgressPercent(const AnalysisTask &task)
{
    if (task.status == QStringLiteral("completed")) return 100.0;
    const double percent = task.type == QStringLiteral("full_rate_batch") ? task.progress * 100.0 : task.progress;
    return std::clamp(percent, 0.0, 100.0);
}

inline QString semanticStatus(const QString &status)
{
    if (status == QStringLiteral("running") || status == QStringLiteral("completed")) return QStringLiteral("success");
    if (status == QStringLiteral("paused") || status == QStringLiteral("partial")) return QStringLiteral("warning");
    if (status == QStringLiteral("failed")) return QStringLiteral("error");
    return QStringLiteral("muted");
}

} // namespace AnalysisUiPresentation

#endif // ANALYSISUIPRESENTATION_H
