#include "analysistaskcenterdialog.h"

#include "analysistaskmanager.h"
#include "analysisuipresentation.h"
#include "animatedbutton.h"

#include <QComboBox>
#include <QDateTime>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QStackedWidget>
#include <QStyle>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

namespace
{
AnimatedButton *button(const QString &text, const QString &variant, QWidget *parent)
{
    auto *result = new AnimatedButton(parent);
    result->setText(text);
    result->setProperty("variant", variant);
    result->setMinimumHeight(36);
    return result;
}

QLabel *sectionTitle(const QString &text, QWidget *parent)
{
    auto *label = new QLabel(text, parent);
    label->setObjectName(QStringLiteral("sectionTitle"));
    return label;
}

QString shownDate(const QDateTime &value)
{
    return value.isValid() ? value.toLocalTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss")) : QStringLiteral("—");
}

bool matchesFilter(const AnalysisTask &task, int filter)
{
    switch (filter)
    {
    case 1:
        return task.status == QStringLiteral("queued") || task.status == QStringLiteral("running");
    case 2:
        return task.status == QStringLiteral("paused");
    case 3:
        return task.status == QStringLiteral("completed");
    case 4:
        return task.status == QStringLiteral("failed");
    case 5:
        return task.status == QStringLiteral("cancelled");
    default:
        return true;
    }
}

int priority(const AnalysisTask &task)
{
    if (task.status == QStringLiteral("running"))
        return 0;
    if (task.status == QStringLiteral("queued"))
        return 1;
    if (task.status == QStringLiteral("paused"))
        return 2;
    return 3;
}
} // namespace

AnalysisTaskCenterDialog::AnalysisTaskCenterDialog(AnalysisTaskManager *manager, QWidget *parent)
    : FramelessDialog(parent), m_manager(manager)
{
    setDialogTitle(QStringLiteral("任务中心"));
    setMinimumSize(1000, 640);
    resize(1180, 720);
    setSizeGripEnabled(true);

    auto *root = contentLayout();
    root->setContentsMargins(18, 14, 18, 16);
    root->setSpacing(10);
    auto *description = new QLabel(QStringLiteral("查看和管理后台分析任务"), this);
    description->setObjectName(QStringLiteral("pageDescription"));
    root->addWidget(description);

    auto *toolbar = new QHBoxLayout;
    m_filter = new QComboBox(this);
    m_filter->addItems({QStringLiteral("全部"), QStringLiteral("进行中"), QStringLiteral("已暂停"),
                        QStringLiteral("已完成"), QStringLiteral("失败"), QStringLiteral("已取消")});
    toolbar->addWidget(new QLabel(QStringLiteral("筛选"), this));
    toolbar->addWidget(m_filter);
    m_summaryLabel = new QLabel(this);
    m_summaryLabel->setObjectName(QStringLiteral("pageDescription"));
    toolbar->addWidget(m_summaryLabel);
    toolbar->addStretch();
    m_syncStatus = new QLabel(this);
    m_syncStatus->setObjectName(QStringLiteral("actionStatus"));
    toolbar->addWidget(m_syncStatus);
    auto *refresh = button(QStringLiteral("刷新"), QStringLiteral("subtle"), this);
    toolbar->addWidget(refresh);
    root->addLayout(toolbar);

    auto *workspace = new QWidget(this);
    workspace->setObjectName(QStringLiteral("settingsBody"));
    auto *workspaceLayout = new QHBoxLayout(workspace);
    workspaceLayout->setContentsMargins(0, 0, 0, 0);
    workspaceLayout->setSpacing(18);

    auto *left = new QWidget(workspace);
    left->setFixedWidth(350);
    auto *leftLayout = new QVBoxLayout(left);
    leftLayout->setContentsMargins(0, 0, 0, 0);
    leftLayout->addWidget(sectionTitle(QStringLiteral("任务列表"), left));
    m_taskList = new QListWidget(left);
    m_taskList->setSelectionMode(QAbstractItemView::SingleSelection);
    m_taskList->setSpacing(3);
    m_taskList->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    leftLayout->addWidget(m_taskList, 1);
    workspaceLayout->addWidget(left);

    auto *right = new QWidget(workspace);
    auto *rightLayout = new QVBoxLayout(right);
    rightLayout->setContentsMargins(0, 0, 0, 0);
    rightLayout->addWidget(sectionTitle(QStringLiteral("任务详情"), right));
    m_detailStack = new QStackedWidget(right);
    auto *empty = new QWidget(m_detailStack);
    auto *emptyLayout = new QVBoxLayout(empty);
    auto *emptyTitle = sectionTitle(QStringLiteral("暂无分析任务"), empty);
    emptyTitle->setAlignment(Qt::AlignCenter);
    auto *emptyText = new QLabel(QStringLiteral("导入视频或创建完整分析后，任务将在这里显示。"), empty);
    emptyTitle->setObjectName(QStringLiteral("taskEmptyTitle"));
    emptyText->setObjectName(QStringLiteral("pageDescription"));
    emptyText->setWordWrap(true);
    emptyText->setAlignment(Qt::AlignCenter);
    emptyLayout->addStretch();
    emptyLayout->addWidget(emptyTitle);
    emptyLayout->addWidget(emptyText);
    emptyLayout->addStretch();
    m_detailStack->addWidget(empty);

    auto *scroll = new QScrollArea(m_detailStack);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    auto *detail = new QWidget(scroll);
    auto *detailLayout = new QVBoxLayout(detail);
    detailLayout->setContentsMargins(0, 0, 8, 0);
    auto *heading = new QHBoxLayout;
    m_detailType = sectionTitle({}, detail);
    m_detailStatus = new QLabel(detail);
    m_detailStatus->setTextFormat(Qt::PlainText);
    m_detailStatus->setProperty("role", QStringLiteral("status"));
    heading->addWidget(m_detailType);
    heading->addStretch();
    heading->addWidget(m_detailStatus);
    detailLayout->addLayout(heading);
    m_progressBar = new QProgressBar(detail);
    m_progressBar->setRange(0, 100);
    m_progressBar->setTextVisible(false);
    m_progressText = new QLabel(detail);
    auto *progressRow = new QHBoxLayout;
    progressRow->addWidget(m_progressBar, 1);
    progressRow->addWidget(m_progressText);
    detailLayout->addLayout(progressRow);
    auto *grid = new QGridLayout;
    grid->setColumnStretch(1, 1);
    auto addRow = [grid, detail](int row, const QString &name, QLabel **value) {
        auto *label = new QLabel(name, detail);
        label->setObjectName(QStringLiteral("settingLabel"));
        *value = new QLabel(detail);
        (*value)->setTextInteractionFlags(Qt::TextSelectableByMouse);
        (*value)->setWordWrap(true);
        grid->addWidget(label, row, 0, Qt::AlignTop);
        grid->addWidget(*value, row, 1);
    };
    addRow(0, QStringLiteral("任务 ID"), &m_taskId);
    addRow(1, QStringLiteral("创建时间"), &m_createdAt);
    addRow(2, QStringLiteral("更新时间"), &m_updatedAt);
    detailLayout->addLayout(grid);
    detailLayout->addWidget(sectionTitle(QStringLiteral("输入摘要"), detail));
    m_inputFields = new QWidget(detail);
    m_inputFieldsLayout = new QVBoxLayout(m_inputFields);
    m_inputFieldsLayout->setContentsMargins(0, 0, 0, 0);
    detailLayout->addWidget(m_inputFields);
    m_rawInputToggle = button(QStringLiteral("查看原始输入"), QStringLiteral("subtle"), detail);
    detailLayout->addWidget(m_rawInputToggle, 0, Qt::AlignLeft);
    m_rawInput = new QPlainTextEdit(detail);
    m_rawInput->setReadOnly(true);
    m_rawInput->setMaximumHeight(150);
    m_rawInput->hide();
    detailLayout->addWidget(m_rawInput);
    detailLayout->addWidget(sectionTitle(QStringLiteral("输出训练"), detail));
    m_outputSession = new QLabel(detail);
    m_outputSession->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_outputSession->setWordWrap(true);
    m_outputSession->setTextFormat(Qt::PlainText);
    detailLayout->addWidget(m_outputSession);
    detailLayout->addWidget(sectionTitle(QStringLiteral("错误信息"), detail));
    m_errorText = new QPlainTextEdit(detail);
    m_errorText->setReadOnly(true);
    m_errorText->setMaximumHeight(100);
    detailLayout->addWidget(m_errorText);
    detailLayout->addStretch();
    scroll->setWidget(detail);
    m_detailStack->addWidget(scroll);
    rightLayout->addWidget(m_detailStack, 1);

    auto *actions = new QHBoxLayout;
    m_pauseButton = button(QStringLiteral("暂停"), QStringLiteral("subtle"), right);
    m_resumeButton = button(QStringLiteral("继续"), QStringLiteral("subtle"), right);
    m_cancelButton = button(QStringLiteral("取消任务"), QStringLiteral("danger"), right);
    m_openSessionButton = button(QStringLiteral("打开训练记录"), QStringLiteral("primary"), right);
    actions->addWidget(m_pauseButton);
    actions->addWidget(m_resumeButton);
    actions->addWidget(m_cancelButton);
    actions->addStretch();
    actions->addWidget(m_openSessionButton);
    rightLayout->addLayout(actions);
    workspaceLayout->addWidget(right, 1);
    root->addWidget(workspace, 1);

    connect(refresh, &QPushButton::clicked, m_manager, &AnalysisTaskManager::refreshTasks);
    connect(m_filter, qOverload<int>(&QComboBox::currentIndexChanged), this, [this]() { reload(selectedTaskId()); });
    connect(m_taskList, &QListWidget::currentRowChanged, this, [this](int) { showTask(selectedTaskId()); });
    connect(m_rawInputToggle, &QPushButton::clicked, this,
            [this]() { m_rawInput->setVisible(!m_rawInput->isVisible()); });
    connect(m_pauseButton, &QPushButton::clicked, this, [this]() {
        if (!selectedTaskId().isEmpty())
            m_manager->pauseTask(selectedTaskId());
    });
    connect(m_resumeButton, &QPushButton::clicked, this, [this]() {
        if (!selectedTaskId().isEmpty())
            m_manager->resumeTask(selectedTaskId());
    });
    connect(m_cancelButton, &QPushButton::clicked, this, [this]() {
        const QString id = selectedTaskId();
        if (id.isEmpty())
            return;
        const auto tasks = m_manager->tasks();
        const auto it =
            std::find_if(tasks.cbegin(), tasks.cend(), [&id](const AnalysisTask &task) { return task.id == id; });
        if (it == tasks.cend() || (it->status != QStringLiteral("queued") && it->status != QStringLiteral("running") &&
                                   it->status != QStringLiteral("paused")))
            return;
        if (QMessageBox::question(this, QStringLiteral("取消任务"), QStringLiteral("确定取消当前后台任务吗？")) !=
            QMessageBox::Yes)
            return;
        const auto current = m_manager->tasks();
        const auto currentTask =
            std::find_if(current.cbegin(), current.cend(), [&id](const AnalysisTask &task) { return task.id == id; });
        if (currentTask != current.cend() &&
            (currentTask->status == QStringLiteral("queued") || currentTask->status == QStringLiteral("running") ||
             currentTask->status == QStringLiteral("paused")))
            m_manager->cancelTask(id);
    });
    connect(m_openSessionButton, &QPushButton::clicked, this, [this]() {
        const QString id = selectedTaskId();
        for (const AnalysisTask &task : m_manager->tasks())
            if (task.id == id && !task.outputSessionId.isEmpty())
                emit openSessionRequested(task.outputSessionId);
    });
    connect(
        m_manager, &AnalysisTaskManager::taskUpdated, this, [this](const QString &) { reload(selectedTaskId()); },
        Qt::QueuedConnection);
    connect(
        m_manager, &AnalysisTaskManager::taskError, this,
        [this](const QString &id, const QString &) {
            if (id.isEmpty())
                m_syncStatus->setText(QStringLiteral("任务服务暂未同步"));
        },
        Qt::QueuedConnection);
    reload();
    m_manager->refreshTasks();
}

void AnalysisTaskCenterDialog::reload(const QString &preferredTaskId)
{
    const QString selection = preferredTaskId.isEmpty() ? selectedTaskId() : preferredTaskId;
    const QSignalBlocker selectionBlocker(m_taskList);
    QVector<AnalysisTask> all = m_manager->tasks();
    std::sort(all.begin(), all.end(), [](const AnalysisTask &a, const AnalysisTask &b) {
        return a.updatedAt == b.updatedAt ? a.createdAt > b.createdAt : a.updatedAt > b.updatedAt;
    });
    int active = 0, failed = 0;
    for (const auto &task : all)
    {
        if (matchesFilter(task, 1))
            ++active;
        if (task.status == QStringLiteral("failed"))
            ++failed;
    }
    m_summaryLabel->setText(
        QStringLiteral("全部 %1  ·  进行中 %2  ·  失败 %3").arg(all.size()).arg(active).arg(failed));
    m_visibleTasks.clear();
    for (const auto &task : all)
        if (matchesFilter(task, m_filter->currentIndex()))
            m_visibleTasks.append(task);
    m_taskList->clear();
    for (const AnalysisTask &task : m_visibleTasks)
    {
        const double percent = AnalysisUiPresentation::taskProgressPercent(task);
        const bool validProgress =
            task.status == QStringLiteral("completed") || (std::isfinite(task.progress) && task.progress >= 0.0);
        auto *item = new QListWidgetItem(m_taskList);
        item->setData(Qt::UserRole, task.id);
        item->setSizeHint(QSize(0, 68));
        auto *entry = new QWidget(m_taskList);
        entry->setAttribute(Qt::WA_TransparentForMouseEvents);
        auto *entryLayout = new QVBoxLayout(entry);
        entryLayout->setContentsMargins(10, 6, 10, 6);
        entryLayout->setSpacing(4);
        auto *top = new QHBoxLayout;
        auto *type = new QLabel(AnalysisUiPresentation::taskType(task.type), entry);
        type->setTextFormat(Qt::PlainText);
        type->setToolTip(type->text());
        type->setMinimumWidth(0);
        type->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
        top->addWidget(type, 1);
        top->addWidget(
            new QLabel(validProgress ? QStringLiteral("%1%").arg(percent, 0, 'f', 0) : QStringLiteral("—"), entry));
        entryLayout->addLayout(top);
        auto *bottom = new QHBoxLayout;
        auto *state = new QLabel(AnalysisUiPresentation::status(task.status), entry);
        state->setTextFormat(Qt::PlainText);
        state->setProperty("state", AnalysisUiPresentation::semanticStatus(task.status));
        bottom->addWidget(state);
        bottom->addStretch();
        auto *updated =
            new QLabel(task.updatedAt.isValid() ? task.updatedAt.toLocalTime().toString(QStringLiteral("MM-dd HH:mm"))
                                                : QStringLiteral("—"),
                       entry);
        updated->setToolTip(shownDate(task.updatedAt));
        updated->setObjectName(QStringLiteral("pageDescription"));
        bottom->addWidget(updated);
        entryLayout->addLayout(bottom);
        m_taskList->setItemWidget(item, entry);
    }
    int row = -1;
    for (int i = 0; i < m_visibleTasks.size(); ++i)
        if (m_visibleTasks.at(i).id == selection)
        {
            row = i;
            break;
        }
    if (row < 0 && !m_visibleTasks.isEmpty())
    {
        row = 0;
        for (int i = 1; i < m_visibleTasks.size(); ++i)
            if (priority(m_visibleTasks.at(i)) < priority(m_visibleTasks.at(row)))
                row = i;
    }
    m_taskList->setCurrentRow(row);
    if (row >= 0)
        showTask(selectedTaskId());
    if (row < 0)
    {
        if (auto *label = m_detailStack->widget(0)->findChild<QLabel *>(QStringLiteral("taskEmptyTitle")))
            label->setText(all.isEmpty() ? QStringLiteral("暂无分析任务") : QStringLiteral("没有符合筛选条件的任务"));
        showTask({});
    }
}

QString AnalysisTaskCenterDialog::selectedTaskId() const
{
    const auto *item = m_taskList->currentItem();
    return item ? item->data(Qt::UserRole).toString() : QString();
}

void AnalysisTaskCenterDialog::showTask(const QString &taskId)
{
    const auto it = std::find_if(m_visibleTasks.cbegin(), m_visibleTasks.cend(),
                                 [&taskId](const AnalysisTask &task) { return task.id == taskId; });
    if (it == m_visibleTasks.cend())
    {
        m_detailStack->setCurrentIndex(0);
        updateActions();
        return;
    }
    const AnalysisTask &task = *it;
    m_detailStack->setCurrentIndex(1);
    m_detailType->setText(AnalysisUiPresentation::taskType(task.type));
    m_detailType->setTextFormat(Qt::PlainText);
    m_detailStatus->setText(AnalysisUiPresentation::status(task.status));
    m_detailStatus->setProperty("state", AnalysisUiPresentation::semanticStatus(task.status));
    m_detailStatus->style()->unpolish(m_detailStatus);
    m_detailStatus->style()->polish(m_detailStatus);
    const double percent = AnalysisUiPresentation::taskProgressPercent(task);
    const bool validProgress =
        task.status == QStringLiteral("completed") || (std::isfinite(task.progress) && task.progress >= 0.0);
    m_progressBar->setVisible(validProgress);
    m_progressText->setText(validProgress ? QStringLiteral("%1%").arg(percent, 0, 'f', 0) : QStringLiteral("—"));
    if (validProgress)
        m_progressBar->setValue(qRound(percent));
    m_taskId->setText(task.id);
    m_createdAt->setText(shownDate(task.createdAt));
    m_updatedAt->setText(shownDate(task.updatedAt));
    while (QLayoutItem *child = m_inputFieldsLayout->takeAt(0))
    {
        delete child->widget();
        delete child;
    }
    QJsonParseError error;
    const QJsonDocument document = QJsonDocument::fromJson(task.inputJson.toUtf8(), &error);
    const QJsonObject object = document.isObject() ? document.object() : QJsonObject();
    const QList<QPair<QString, QString>> keys = {{QStringLiteral("videoPath"), QStringLiteral("文件")},
                                                 {QStringLiteral("fileName"), QStringLiteral("文件")},
                                                 {QStringLiteral("cameraCount"), QStringLiteral("Camera 数量")},
                                                 {QStringLiteral("batchId"), QStringLiteral("Batch ID")},
                                                 {QStringLiteral("runId"), QStringLiteral("Run ID")}};
    bool hasSummary = false;
    for (const auto &entry : keys)
        if (object.contains(entry.first) && !object.value(entry.first).isObject() &&
            !object.value(entry.first).isArray())
        {
            auto *value = new QLabel(
                QStringLiteral("%1    %2").arg(entry.second, object.value(entry.first).toVariant().toString()),
                m_inputFields);
            value->setTextFormat(Qt::PlainText);
            value->setWordWrap(true);
            value->setTextInteractionFlags(Qt::TextSelectableByMouse);
            m_inputFieldsLayout->addWidget(value);
            hasSummary = true;
        }
    if (!hasSummary)
        m_inputFieldsLayout->addWidget(new QLabel(QStringLiteral("无可识别的摘要字段"), m_inputFields));
    m_rawInput->setPlainText(error.error == QJsonParseError::NoError
                                 ? QString::fromUtf8(document.toJson(QJsonDocument::Indented))
                                 : task.inputJson);
    if (m_rawInput->property("taskId").toString() != task.id)
        m_rawInput->hide();
    m_rawInput->setProperty("taskId", task.id);
    m_outputSession->setText(task.outputSessionId.isEmpty() ? QStringLiteral("—") : task.outputSessionId);
    m_errorText->setPlainText(task.errorMessage.isEmpty() ? QStringLiteral("—") : task.errorMessage);
    updateActions();
}

void AnalysisTaskCenterDialog::updateActions()
{
    QString status, output;
    for (const auto &task : m_visibleTasks)
        if (task.id == selectedTaskId())
        {
            status = task.status;
            output = task.outputSessionId;
            break;
        }
    const bool canPause = status == QStringLiteral("queued") || status == QStringLiteral("running");
    const bool canResume = status == QStringLiteral("paused");
    const bool canCancel = canPause || canResume;
    const bool canOpen = status == QStringLiteral("completed") && !output.isEmpty();
    m_pauseButton->setVisible(canPause);
    m_pauseButton->setEnabled(canPause);
    m_resumeButton->setVisible(canResume);
    m_resumeButton->setEnabled(canResume);
    m_cancelButton->setVisible(canCancel);
    m_cancelButton->setEnabled(canCancel);
    m_openSessionButton->setVisible(canOpen);
    m_openSessionButton->setEnabled(canOpen);
}
