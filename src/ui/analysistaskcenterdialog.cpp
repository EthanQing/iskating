#include "analysistaskcenterdialog.h"

#include "analysistaskmanager.h"

#include <QDateTime>
#include <QDialogButtonBox>
#include <QAbstractItemView>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPushButton>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>

AnalysisTaskCenterDialog::AnalysisTaskCenterDialog(AnalysisTaskManager *manager, QWidget *parent)
    : QDialog(parent), m_manager(manager)
{
    setWindowTitle(QStringLiteral("任务中心"));
    resize(1080, 520);
    auto *layout = new QVBoxLayout(this);
    m_table = new QTableWidget(this);
    m_table->setColumnCount(8);
    m_table->setHorizontalHeaderLabels({QStringLiteral("类型"), QStringLiteral("状态"), QStringLiteral("进度"),
                                        QStringLiteral("输入摘要"), QStringLiteral("输出训练"), QStringLiteral("错误"),
                                        QStringLiteral("创建时间"), QStringLiteral("更新时间")});
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->horizontalHeader()->setStretchLastSection(true);
    layout->addWidget(m_table);

    auto *actions = new QHBoxLayout;
    m_pauseButton = new QPushButton(QStringLiteral("暂停"), this);
    m_resumeButton = new QPushButton(QStringLiteral("继续"), this);
    m_cancelButton = new QPushButton(QStringLiteral("取消"), this);
    m_openSessionButton = new QPushButton(QStringLiteral("打开训练记录"), this);
    auto *refreshButton = new QPushButton(QStringLiteral("刷新"), this);
    actions->addWidget(m_pauseButton);
    actions->addWidget(m_resumeButton);
    actions->addWidget(m_cancelButton);
    actions->addWidget(m_openSessionButton);
    actions->addStretch();
    actions->addWidget(refreshButton);
    layout->addLayout(actions);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);

    connect(refreshButton, &QPushButton::clicked, this, [this]() { m_manager->refreshTasks(); });
    connect(m_pauseButton, &QPushButton::clicked, this, [this]() { m_manager->pauseTask(selectedTaskId()); });
    connect(m_resumeButton, &QPushButton::clicked, this, [this]() { m_manager->resumeTask(selectedTaskId()); });
    connect(m_cancelButton, &QPushButton::clicked, this, [this]() { m_manager->cancelTask(selectedTaskId()); });
    connect(m_openSessionButton, &QPushButton::clicked, this, [this]() {
        const int row = m_table->currentRow();
        if (row < 0) return;
        const QString sessionId = m_table->item(row, 4)->data(Qt::UserRole).toString();
        if (!sessionId.isEmpty()) emit openSessionRequested(sessionId);
    });
    connect(m_manager, &AnalysisTaskManager::taskUpdated, this, [this](const QString &) { reload(); }, Qt::QueuedConnection);
    connect(m_table, &QTableWidget::itemSelectionChanged, this, [this]() {
        const int row = m_table->currentRow();
        const bool selected = row >= 0;
        const QString status = selected ? m_table->item(row, 1)->data(Qt::UserRole).toString() : QString();
        m_pauseButton->setEnabled(status == QStringLiteral("queued") || status == QStringLiteral("running"));
        m_resumeButton->setEnabled(status == QStringLiteral("paused"));
        m_cancelButton->setEnabled(status == QStringLiteral("queued") || status == QStringLiteral("running") || status == QStringLiteral("paused"));
        m_openSessionButton->setEnabled(selected && !m_table->item(row, 4)->data(Qt::UserRole).toString().isEmpty());
    });
    reload();
    m_manager->refreshTasks();
}

void AnalysisTaskCenterDialog::reload()
{
    const QVector<AnalysisTask> tasks = m_manager->tasks();
    m_table->setRowCount(tasks.size());
    for (int row = 0; row < tasks.size(); ++row) {
        const AnalysisTask &task = tasks.at(row);
        QJsonParseError parseError;
        const QJsonDocument input = QJsonDocument::fromJson(task.inputJson.toUtf8(), &parseError);
        const QString inputText = parseError.error == QJsonParseError::NoError
                                      ? QString::fromUtf8(QJsonDocument(input.object()).toJson(QJsonDocument::Compact))
                                      : task.inputJson;
        auto set = [this, row](int column, const QString &text, const QVariant &data = {}) {
            auto *item = new QTableWidgetItem(text);
            if (data.isValid()) item->setData(Qt::UserRole, data);
            m_table->setItem(row, column, item);
        };
        set(0, task.type, task.id);
        set(1, task.status, task.status);
        set(2, QStringLiteral("%1%").arg(task.progress, 0, 'f', 0));
        set(3, inputText);
        set(4, task.outputSessionId.left(12), task.outputSessionId);
        set(5, task.errorMessage);
        set(6, task.createdAt.toString(Qt::ISODate));
        set(7, task.updatedAt.toString(Qt::ISODate));
    }
    m_table->resizeColumnsToContents();
}

QString AnalysisTaskCenterDialog::selectedTaskId() const
{
    const int row = m_table->currentRow();
    return row < 0 ? QString() : m_table->item(row, 0)->data(Qt::UserRole).toString();
}
