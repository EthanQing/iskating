#include "competitionmanagementdialog.h"

#include "animatedbutton.h"
#include "trainingrepository.h"

#include <QAbstractItemView>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDateEdit>
#include <QDateTimeEdit>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScreen>
#include <QScrollArea>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QSizePolicy>
#include <QSpinBox>
#include <QStackedWidget>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>

#include <algorithm>
#include <limits>

namespace
{

AnimatedButton *makeButton(const QString &text, const QString &variant, QWidget *parent)
{
    auto *button = new AnimatedButton(parent);
    button->setText(text);
    button->setProperty("variant", variant);
    button->setMinimumHeight(36);
    return button;
}

QLabel *sectionTitle(const QString &text, QWidget *parent)
{
    auto *label = new QLabel(text, parent);
    label->setObjectName(QStringLiteral("sectionTitle"));
    return label;
}

void configureTable(QTableWidget *table)
{
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setSelectionMode(QAbstractItemView::SingleSelection);
    table->setShowGrid(false);
    table->setWordWrap(false);
    table->setTextElideMode(Qt::ElideRight);
    table->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    table->verticalHeader()->setVisible(false);
    table->verticalHeader()->setDefaultSectionSize(42);
    table->horizontalHeader()->setHighlightSections(false);
    table->horizontalHeader()->setSectionsClickable(false);
}

QTableWidgetItem *item(const QString &text, const QString &id = {})
{
    auto *result = new QTableWidgetItem(text);
    result->setFlags(result->flags() & ~Qt::ItemIsEditable);
    result->setToolTip(text);
    if (!id.isEmpty())
        result->setData(Qt::UserRole, id);
    return result;
}

void addField(QGridLayout *grid, int row, int column, const QString &text, QWidget *editor, int columnSpan = 1)
{
    auto *field = new QWidget(editor->parentWidget());
    auto *layout = new QVBoxLayout(field);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(5);
    auto *label = new QLabel(text, field);
    label->setObjectName(QStringLiteral("settingLabel"));
    layout->addWidget(label);
    layout->addWidget(editor);
    grid->addWidget(field, row, column, 1, columnSpan);
}

QWidget *emptyState(const QString &title, const QString &description, QPushButton **button, const QString &buttonText,
                    QWidget *parent)
{
    auto *state = new QWidget(parent);
    auto *layout = new QVBoxLayout(state);
    layout->setContentsMargins(16, 16, 16, 16);
    layout->addStretch();
    auto *titleLabel = sectionTitle(title, state);
    titleLabel->setAlignment(Qt::AlignCenter);
    layout->addWidget(titleLabel);
    if (!description.isEmpty())
    {
        auto *descriptionLabel = new QLabel(description, state);
        descriptionLabel->setObjectName(QStringLiteral("pageDescription"));
        descriptionLabel->setWordWrap(true);
        descriptionLabel->setAlignment(Qt::AlignCenter);
        layout->addWidget(descriptionLabel);
    }
    *button = makeButton(buttonText, QStringLiteral("subtle"), state);
    layout->addWidget(*button, 0, Qt::AlignHCenter);
    layout->addStretch();
    return state;
}

QString shown(const QString &value, const QString &fallback = QStringLiteral("未填写"))
{
    return value.trimmed().isEmpty() ? fallback : value.trimmed();
}

} // namespace

CompetitionManagementDialog::CompetitionManagementDialog(TrainingRepository *repository, QWidget *parent)
    : FramelessDialog(parent), m_repository(repository)
{
    setDialogTitle(QStringLiteral("比赛管理"));
    setMinimumSize(1040, 680);
    QSize size(1200, 760);
    if (const QScreen *screen = QApplication::primaryScreen())
    {
        const QSize available = screen->availableGeometry().size() - QSize(24, 24);
        size.setWidth(std::max(1040, std::min(size.width(), available.width())));
        size.setHeight(std::max(680, std::min(size.height(), available.height())));
    }
    resize(size);
    setSizeGripEnabled(true);
    if (auto *titleBar = findChild<QWidget *>(QStringLiteral("dialogTitleBar")))
        titleBar->setFixedHeight(48);
    buildUi();
    ensurePolished();
    m_competitionNotes->setFixedHeight(68);
    m_eventNotes->setFixedHeight(62);
    m_eventAthleteNotes->setFixedHeight(62);
    reloadCompetitions();
    setStatus({});
}

bool CompetitionManagementDialog::changed() const
{
    return m_changed;
}

void CompetitionManagementDialog::buildUi()
{
    auto *root = contentLayout();
    root->setContentsMargins(18, 14, 18, 16);
    root->setSpacing(10);
    auto *description = new QLabel(QStringLiteral("维护比赛、场次及参赛运动员信息"), this);
    description->setObjectName(QStringLiteral("pageDescription"));
    root->addWidget(description);
    m_serviceWarning = new QLabel(QStringLiteral("训练服务未连接，暂时无法管理比赛。"), this);
    m_serviceWarning->setObjectName(QStringLiteral("actionStatus"));
    root->addWidget(m_serviceWarning);

    auto *workspace = new QWidget(this);
    workspace->setObjectName(QStringLiteral("settingsBody"));
    auto *workspaceLayout = new QHBoxLayout(workspace);
    workspaceLayout->setContentsMargins(0, 0, 0, 0);
    workspaceLayout->setSpacing(18);

    auto *left = new QWidget(workspace);
    left->setFixedWidth(300);
    auto *leftLayout = new QVBoxLayout(left);
    leftLayout->setContentsMargins(0, 0, 0, 0);
    leftLayout->setSpacing(10);
    auto *leftHeader = new QHBoxLayout();
    leftHeader->addWidget(sectionTitle(QStringLiteral("比赛列表"), left));
    leftHeader->addStretch();
    m_newCompetitionButton = makeButton(QStringLiteral("新增比赛"), QStringLiteral("subtle"), left);
    leftHeader->addWidget(m_newCompetitionButton);
    leftLayout->addLayout(leftHeader);
    auto *searchRow = new QHBoxLayout();
    m_searchEdit = new QLineEdit(left);
    m_searchEdit->setPlaceholderText(QStringLiteral("搜索比赛..."));
    m_searchEdit->setClearButtonEnabled(true);
    searchRow->addWidget(m_searchEdit, 1);
    m_competitionCount = new QLabel(left);
    m_competitionCount->setObjectName(QStringLiteral("pageDescription"));
    searchRow->addWidget(m_competitionCount);
    leftLayout->addLayout(searchRow);
    m_competitionStack = new QStackedWidget(left);
    m_competitionTable = new QTableWidget(0, 1, m_competitionStack);
    m_competitionTable->setObjectName(QStringLiteral("competitionTable"));
    configureTable(m_competitionTable);
    m_competitionTable->horizontalHeader()->hide();
    m_competitionTable->verticalHeader()->setDefaultSectionSize(68);
    m_competitionTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_competitionStack->addWidget(m_competitionTable);
    m_competitionStack->addWidget(
        emptyState(QStringLiteral("暂无比赛"), QStringLiteral("创建比赛后，\n即可维护场次和参赛运动员。"),
                   &m_emptyCompetitionButton, QStringLiteral("新增比赛"), m_competitionStack));
    auto *noMatch = new QLabel(QStringLiteral("没有匹配的比赛"), m_competitionStack);
    noMatch->setObjectName(QStringLiteral("pageDescription"));
    noMatch->setAlignment(Qt::AlignCenter);
    m_competitionStack->addWidget(noMatch);
    leftLayout->addWidget(m_competitionStack, 1);
    workspaceLayout->addWidget(left);

    auto *scroll = new QScrollArea(workspace);
    m_detailScroll = scroll;
    scroll->setObjectName(QStringLiteral("settingsPage"));
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    auto *detail = new QWidget(scroll);
    auto *detailLayout = new QVBoxLayout(detail);
    detailLayout->setContentsMargins(0, 2, 10, 8);
    detailLayout->setSpacing(10);
    m_competitionTitle = new QLabel(QStringLiteral("比赛资料"), detail);
    m_competitionTitle->setObjectName(QStringLiteral("pageTitle"));
    detailLayout->addWidget(m_competitionTitle);
    m_competitionEditors = new QWidget(detail);
    auto *competitionGrid = new QGridLayout(m_competitionEditors);
    competitionGrid->setContentsMargins(0, 0, 0, 0);
    competitionGrid->setSpacing(10);
    competitionGrid->setColumnStretch(0, 1);
    competitionGrid->setColumnStretch(1, 1);
    m_nameEdit = new QLineEdit(m_competitionEditors);
    m_typeEdit = new QLineEdit(m_competitionEditors);
    m_nameEdit->setObjectName(QStringLiteral("competitionNameEdit"));
    m_locationEdit = new QLineEdit(m_competitionEditors);
    m_dateEdit = new QDateEdit(m_competitionEditors);
    m_dateEdit->setCalendarPopup(true);
    m_dateEdit->setDisplayFormat(QStringLiteral("yyyy-MM-dd"));
    m_dateEnabled = new QCheckBox(QStringLiteral("设置日期"), m_competitionEditors);
    auto *dateBox = new QWidget(m_competitionEditors);
    auto *dateLayout = new QHBoxLayout(dateBox);
    dateLayout->setContentsMargins(0, 0, 0, 0);
    dateLayout->addWidget(m_dateEnabled);
    dateLayout->addWidget(m_dateEdit, 1);
    addField(competitionGrid, 0, 0, QStringLiteral("比赛名称"), m_nameEdit);
    addField(competitionGrid, 0, 1, QStringLiteral("比赛类型"), m_typeEdit);
    addField(competitionGrid, 1, 0, QStringLiteral("比赛日期"), dateBox);
    addField(competitionGrid, 1, 1, QStringLiteral("地点"), m_locationEdit);
    m_competitionNotes = new QPlainTextEdit(m_competitionEditors);
    m_competitionNotes->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    addField(competitionGrid, 2, 0, QStringLiteral("备注"), m_competitionNotes, 2);
    detailLayout->addWidget(m_competitionEditors);
    auto *competitionActions = new QHBoxLayout();
    m_archiveCompetitionButton = makeButton(QStringLiteral("归档比赛"), QStringLiteral("danger"), detail);
    m_saveCompetitionButton = makeButton(QStringLiteral("保存"), QStringLiteral("primary"), detail);
    competitionActions->addWidget(m_archiveCompetitionButton);
    competitionActions->addStretch();
    competitionActions->addWidget(m_saveCompetitionButton);
    detailLayout->addLayout(competitionActions);

    auto *eventHeader = new QHBoxLayout();
    eventHeader->addWidget(sectionTitle(QStringLiteral("比赛场次"), detail));
    eventHeader->addStretch();
    m_eventCount = new QLabel(detail);
    m_eventCount->setObjectName(QStringLiteral("pageDescription"));
    eventHeader->addWidget(m_eventCount);
    m_newEventButton = makeButton(QStringLiteral("新增场次"), QStringLiteral("subtle"), detail);
    eventHeader->addWidget(m_newEventButton);
    detailLayout->addLayout(eventHeader);
    m_eventParentHint = new QLabel(QStringLiteral("请先保存比赛，再添加场次。"), detail);
    m_eventParentHint->setObjectName(QStringLiteral("pageDescription"));
    detailLayout->addWidget(m_eventParentHint);
    auto *eventBody = new QWidget(detail);
    m_eventBody = eventBody;
    auto *eventBodyLayout = new QHBoxLayout(eventBody);
    eventBodyLayout->setContentsMargins(0, 0, 0, 0);
    eventBodyLayout->setSpacing(14);
    m_eventStack = new QStackedWidget(eventBody);
    m_eventStack->setMinimumHeight(185);
    m_eventTable = new QTableWidget(0, 3, m_eventStack);
    m_eventTable->setHorizontalHeaderLabels(
        {QStringLiteral("场次"), QStringLiteral("组别"), QStringLiteral("计划时间")});
    configureTable(m_eventTable);
    m_eventTable->setObjectName(QStringLiteral("competitionEventTable"));
    m_eventTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_eventTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_eventTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    m_eventStack->addWidget(m_eventTable);
    m_eventStack->addWidget(
        emptyState(QStringLiteral("暂无比赛场次"), {}, &m_emptyEventButton, QStringLiteral("新增场次"), m_eventStack));
    eventBodyLayout->addWidget(m_eventStack, 43);
    auto *eventDetail = new QWidget(eventBody);
    m_eventDetail = eventDetail;
    auto *eventDetailLayout = new QVBoxLayout(eventDetail);
    eventDetailLayout->setContentsMargins(0, 0, 0, 0);
    eventDetailLayout->setSpacing(7);
    m_eventTitle = sectionTitle(QStringLiteral("场次详情"), eventDetail);
    eventDetailLayout->addWidget(m_eventTitle);
    m_eventEditors = new QWidget(eventDetail);
    auto *eventGrid = new QGridLayout(m_eventEditors);
    eventGrid->setContentsMargins(0, 0, 0, 0);
    eventGrid->setSpacing(7);
    eventGrid->setColumnStretch(0, 1);
    eventGrid->setColumnStretch(1, 1);
    m_raceEdit = new QLineEdit(m_eventEditors);
    m_eventNameEdit = new QLineEdit(m_eventEditors);
    m_groupEdit = new QLineEdit(m_eventEditors);
    m_heatEdit = new QLineEdit(m_eventEditors);
    m_scheduledEnabled = new QCheckBox(QStringLiteral("设置时间"), m_eventEditors);
    m_scheduledEdit = new QDateTimeEdit(m_eventEditors);
    m_scheduledEdit->setCalendarPopup(true);
    m_scheduledEdit->setDisplayFormat(QStringLiteral("yyyy-MM-dd HH:mm"));
    auto *timeBox = new QWidget(m_eventEditors);
    auto *timeLayout = new QHBoxLayout(timeBox);
    timeLayout->setContentsMargins(0, 0, 0, 0);
    timeLayout->addWidget(m_scheduledEnabled);
    timeLayout->addWidget(m_scheduledEdit, 1);
    addField(eventGrid, 0, 0, QStringLiteral("项目 / 场次名称"), m_raceEdit);
    addField(eventGrid, 0, 1, QStringLiteral("比赛项目"), m_eventNameEdit);
    addField(eventGrid, 1, 0, QStringLiteral("组别"), m_groupEdit);
    addField(eventGrid, 1, 1, QStringLiteral("分组 / 轮次"), m_heatEdit);
    addField(eventGrid, 2, 0, QStringLiteral("计划时间"), timeBox, 2);
    m_eventNotes = new QPlainTextEdit(m_eventEditors);
    m_eventNotes->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    addField(eventGrid, 3, 0, QStringLiteral("备注"), m_eventNotes, 2);
    eventDetailLayout->addWidget(m_eventEditors);
    auto *eventActions = new QHBoxLayout();
    m_archiveEventButton = makeButton(QStringLiteral("归档场次"), QStringLiteral("danger"), eventDetail);
    m_saveEventButton = makeButton(QStringLiteral("保存场次"), QStringLiteral("primary"), eventDetail);
    eventActions->addWidget(m_archiveEventButton);
    eventActions->addStretch();
    eventActions->addWidget(m_saveEventButton);
    eventDetailLayout->addLayout(eventActions);
    eventBodyLayout->addWidget(eventDetail, 57);
    detailLayout->addWidget(eventBody);

    auto *athleteHeader = new QHBoxLayout();
    athleteHeader->addWidget(sectionTitle(QStringLiteral("参赛运动员"), detail));
    athleteHeader->addStretch();
    m_eventAthleteCount = new QLabel(detail);
    m_eventAthleteCount->setObjectName(QStringLiteral("pageDescription"));
    athleteHeader->addWidget(m_eventAthleteCount);
    m_newEventAthleteButton = makeButton(QStringLiteral("添加运动员"), QStringLiteral("subtle"), detail);
    athleteHeader->addWidget(m_newEventAthleteButton);
    detailLayout->addLayout(athleteHeader);
    m_athleteParentHint = new QLabel(QStringLiteral("请先保存并选择场次，再添加运动员。"), detail);
    m_athleteParentHint->setObjectName(QStringLiteral("pageDescription"));
    detailLayout->addWidget(m_athleteParentHint);
    auto *athleteBody = new QWidget(detail);
    m_athleteBody = athleteBody;
    auto *athleteBodyLayout = new QVBoxLayout(athleteBody);
    athleteBodyLayout->setContentsMargins(0, 0, 0, 0);
    athleteBodyLayout->setSpacing(10);
    m_eventAthleteStack = new QStackedWidget(athleteBody);
    m_eventAthleteStack->setMinimumHeight(190);
    m_eventAthleteTable = new QTableWidget(0, 6, m_eventAthleteStack);
    m_eventAthleteTable->setHorizontalHeaderLabels({QStringLiteral("运动员"), QStringLiteral("号码"),
                                                    QStringLiteral("道次"), QStringLiteral("顺序"),
                                                    QStringLiteral("分数"), QStringLiteral("名次")});
    configureTable(m_eventAthleteTable);
    m_eventAthleteTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    m_eventAthleteStack->addWidget(m_eventAthleteTable);
    m_eventAthleteStack->addWidget(emptyState(QStringLiteral("暂无参赛运动员"), {}, &m_emptyEventAthleteButton,
                                              QStringLiteral("添加运动员"), m_eventAthleteStack));
    athleteBodyLayout->addWidget(m_eventAthleteStack);
    m_eventAthleteTable->setObjectName(QStringLiteral("eventAthleteTable"));
    m_eventAthleteEditors = new QWidget(athleteBody);
    auto *athleteEditorLayout = new QVBoxLayout(m_eventAthleteEditors);
    athleteEditorLayout->setContentsMargins(0, 0, 0, 0);
    athleteEditorLayout->setSpacing(7);
    athleteEditorLayout->addWidget(sectionTitle(QStringLiteral("参赛信息"), m_eventAthleteEditors));
    auto *athleteGrid = new QGridLayout();
    athleteGrid->setSpacing(7);
    athleteGrid->setColumnStretch(0, 1);
    athleteGrid->setColumnStretch(1, 1);
    athleteGrid->setColumnStretch(2, 1);
    m_athleteCombo = new QComboBox(m_eventAthleteEditors);
    m_bibEdit = new QLineEdit(m_eventAthleteEditors);
    m_laneEdit = new QLineEdit(m_eventAthleteEditors);
    m_sortSpin = new QSpinBox(m_eventAthleteEditors);
    m_scoreSpin = new QSpinBox(m_eventAthleteEditors);
    m_rankSpin = new QSpinBox(m_eventAthleteEditors);
    m_sortSpin->setRange(0, std::numeric_limits<int>::max());
    m_scoreSpin->setRange(-1, std::numeric_limits<int>::max());
    m_scoreSpin->setSpecialValueText(QStringLiteral("未录入"));
    m_rankSpin->setRange(-1, std::numeric_limits<int>::max());
    m_rankSpin->setSpecialValueText(QStringLiteral("未录入"));
    m_athleteCombo->setObjectName(QStringLiteral("eventAthleteCombo"));
    addField(athleteGrid, 0, 0, QStringLiteral("运动员"), m_athleteCombo);
    addField(athleteGrid, 0, 1, QStringLiteral("号码"), m_bibEdit);
    addField(athleteGrid, 0, 2, QStringLiteral("道次"), m_laneEdit);
    addField(athleteGrid, 1, 0, QStringLiteral("顺序"), m_sortSpin);
    addField(athleteGrid, 1, 1, QStringLiteral("成绩"), m_scoreSpin);
    addField(athleteGrid, 1, 2, QStringLiteral("名次"), m_rankSpin);
    m_eventAthleteNotes = new QPlainTextEdit(m_eventAthleteEditors);
    m_eventAthleteNotes->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    addField(athleteGrid, 2, 0, QStringLiteral("备注"), m_eventAthleteNotes, 3);
    athleteEditorLayout->addLayout(athleteGrid);
    auto *athleteActions = new QHBoxLayout();
    m_removeEventAthleteButton =
        makeButton(QStringLiteral("移出场次"), QStringLiteral("danger"), m_eventAthleteEditors);
    m_saveEventAthleteButton =
        makeButton(QStringLiteral("保存参赛信息"), QStringLiteral("primary"), m_eventAthleteEditors);
    athleteActions->addWidget(m_removeEventAthleteButton);
    athleteActions->addStretch();
    athleteActions->addWidget(m_saveEventAthleteButton);
    athleteEditorLayout->addLayout(athleteActions);
    athleteBodyLayout->addWidget(m_eventAthleteEditors);
    detailLayout->addWidget(athleteBody);
    detailLayout->addStretch();
    scroll->setWidget(detail);
    workspaceLayout->addWidget(scroll, 1);
    root->addWidget(workspace, 1);
    m_statusLabel = new QLabel(this);
    m_statusLabel->setObjectName(QStringLiteral("actionStatus"));
    m_statusLabel->setMinimumHeight(18);
    root->addWidget(m_statusLabel);

    connect(m_searchEdit, &QLineEdit::textChanged, this, [this] { applyCompetitionFilter(); });
    connect(m_competitionTable, &QTableWidget::currentCellChanged, this,
            [this](int row, int, int, int) { selectCompetition(row); });
    connect(m_eventTable, &QTableWidget::currentCellChanged, this,
            [this](int row, int, int, int) { selectEvent(row); });
    connect(m_eventAthleteTable, &QTableWidget::currentCellChanged, this,
            [this](int row, int, int, int) { selectEventAthlete(row); });
    connect(m_newCompetitionButton, &QPushButton::clicked, this, [this] { newCompetition(); });
    connect(m_emptyCompetitionButton, &QPushButton::clicked, this, [this] { newCompetition(); });
    connect(m_newEventButton, &QPushButton::clicked, this, [this] { newEvent(); });
    connect(m_emptyEventButton, &QPushButton::clicked, this, [this] { newEvent(); });
    connect(m_newEventAthleteButton, &QPushButton::clicked, this, [this] { newEventAthlete(); });
    connect(m_emptyEventAthleteButton, &QPushButton::clicked, this, [this] { newEventAthlete(); });
    connect(m_saveCompetitionButton, &QPushButton::clicked, this, [this] { saveCompetition(); });
    connect(m_archiveCompetitionButton, &QPushButton::clicked, this, [this] { archiveCompetition(); });
    connect(m_saveEventButton, &QPushButton::clicked, this, [this] { saveEvent(); });
    connect(m_archiveEventButton, &QPushButton::clicked, this, [this] { archiveEvent(); });
    connect(m_saveEventAthleteButton, &QPushButton::clicked, this, [this] { saveEventAthlete(); });
    connect(m_removeEventAthleteButton, &QPushButton::clicked, this, [this] { removeEventAthlete(); });
    connect(m_dateEnabled, &QCheckBox::toggled, m_dateEdit, &QWidget::setEnabled);
    connect(m_scheduledEnabled, &QCheckBox::toggled, m_scheduledEdit, &QWidget::setEnabled);
}

void CompetitionManagementDialog::reloadCompetitions(const QString &selectedId)
{
    m_competitions.clear();
    m_athletes.clear();
    if (m_repository && m_repository->isOpen())
    {
        m_competitions = m_repository->competitions();
        m_athletes = m_repository->athletes();
    }
    applyCompetitionFilter();
    if (m_competitionTable->rowCount() == 0 && !m_competitions.isEmpty())
    {
        const QSignalBlocker blocker(m_searchEdit);
        m_searchEdit->clear();
        applyCompetitionFilter();
    }
    int row = competitionRowForId(selectedId);
    if (row < 0 && m_competitionTable->rowCount() > 0)
        row = 0;
    if (row >= 0)
    {
        QSignalBlocker blocker(m_competitionTable);
        m_competitionTable->setCurrentCell(row, 0);
        selectCompetition(row);
    }
    else
        selectCompetition(-1);
    updateAvailability();
}

void CompetitionManagementDialog::applyCompetitionFilter()
{
    const QString query = m_searchEdit->text().trimmed();
    const QString selectedId = m_currentCompetitionId;
    QSignalBlocker blocker(m_competitionTable);
    m_competitionTable->setRowCount(0);
    int visible = 0;
    int selectedRow = -1;
    for (const Competition &competition : std::as_const(m_competitions))
    {
        if (!query.isEmpty() && !competition.name.contains(query, Qt::CaseInsensitive) &&
            !competition.location.contains(query, Qt::CaseInsensitive) &&
            !competition.competitionType.contains(query, Qt::CaseInsensitive))
            continue;
        const int row = m_competitionTable->rowCount();
        m_competitionTable->insertRow(row);
        const QString date = competition.competitionDate.isValid()
                                 ? competition.competitionDate.toString(QStringLiteral("yyyy-MM-dd"))
                                 : QStringLiteral("未设置日期");
        const QString summary =
            QStringLiteral("%1\n%2 · %3")
                .arg(shown(competition.name), date, shown(competition.competitionType, QStringLiteral("未设置类型")));
        auto *nameItem = item(summary, competition.id);
        nameItem->setToolTip(QStringLiteral("%1\n%2").arg(competition.name, shown(competition.location)));
        m_competitionTable->setItem(row, 0, nameItem);
        if (competition.id == selectedId)
            selectedRow = row;
        ++visible;
    }
    m_competitionCount->setText(query.isEmpty() ? QStringLiteral("共 %1 场").arg(m_competitions.size())
                                                : QStringLiteral("%1 / %2").arg(visible).arg(m_competitions.size()));
    m_competitionStack->setCurrentIndex(m_competitions.isEmpty() ? 1 : (visible == 0 ? 2 : 0));
    if (selectedRow >= 0)
        m_competitionTable->setCurrentCell(selectedRow, 0);
}

void CompetitionManagementDialog::selectCompetition(int row)
{
    Competition selected;
    if (row >= 0 && row < m_competitionTable->rowCount())
    {
        const QString id = m_competitionTable->item(row, 0)->data(Qt::UserRole).toString();
        for (const Competition &competition : std::as_const(m_competitions))
            if (competition.id == id)
            {
                selected = competition;
                break;
            }
    }
    m_currentCompetitionId = selected.id;
    setCompetitionForm(selected);
    reloadEvents();
    updateAvailability();
    m_detailScroll->verticalScrollBar()->setValue(0);
}

void CompetitionManagementDialog::reloadEvents(const QString &selectedId)
{
    m_events = (m_repository && m_repository->isOpen() && !m_currentCompetitionId.isEmpty())
                   ? m_repository->competitionEvents(m_currentCompetitionId)
                   : QVector<CompetitionEvent>();
    QSignalBlocker blocker(m_eventTable);
    m_eventTable->setCurrentCell(-1, -1);
    m_eventTable->clearSelection();
    m_eventTable->setRowCount(m_events.size());
    for (int row = 0; row < m_events.size(); ++row)
    {
        const CompetitionEvent &event = m_events.at(row);
        const QString primary = !event.raceName.trimmed().isEmpty() ? event.raceName : event.eventName;
        m_eventTable->setItem(row, 0, item(shown(primary), event.id));
        m_eventTable->setItem(row, 1, item(shown(event.groupName, shown(event.heatName, QStringLiteral("-")))));
        auto *scheduledItem =
            item(event.scheduledAt.isValid() ? event.scheduledAt.toLocalTime().toString(QStringLiteral("MM-dd HH:mm"))
                                             : QStringLiteral("未设置"));
        if (event.scheduledAt.isValid())
            scheduledItem->setToolTip(event.scheduledAt.toLocalTime().toString(QStringLiteral("yyyy-MM-dd HH:mm")));
        m_eventTable->setItem(row, 2, scheduledItem);
    }
    m_eventCount->setText(QStringLiteral("共 %1 个场次").arg(m_events.size()));
    m_eventStack->setCurrentIndex(m_events.isEmpty() ? 1 : 0);
    int row = eventRowForId(selectedId);
    if (row < 0 && !m_events.isEmpty())
        row = 0;
    if (row >= 0)
    {
        m_eventTable->setCurrentCell(row, 0);
        selectEvent(row);
    }
    else
        selectEvent(-1);
}

void CompetitionManagementDialog::selectEvent(int row)
{
    m_creatingEvent = false;
    CompetitionEvent selected;
    if (row >= 0 && row < m_events.size())
        selected = m_events.at(row);
    selected.competitionId = m_currentCompetitionId;
    m_currentEventId = selected.id;
    setEventForm(selected);
    reloadEventAthletes();
    updateAvailability();
}

void CompetitionManagementDialog::reloadEventAthletes(const QString &selectedId)
{
    m_eventAthletes = (m_repository && m_repository->isOpen() && !m_currentEventId.isEmpty())
                          ? m_repository->eventAthletes(m_currentEventId)
                          : QVector<EventAthlete>();
    QSignalBlocker blocker(m_eventAthleteTable);
    m_eventAthleteTable->setCurrentCell(-1, -1);
    m_eventAthleteTable->clearSelection();
    m_eventAthleteTable->setRowCount(m_eventAthletes.size());
    for (int row = 0; row < m_eventAthletes.size(); ++row)
    {
        const EventAthlete &entry = m_eventAthletes.at(row);
        m_eventAthleteTable->setItem(row, 0, item(shown(entry.athleteName, entry.athleteId), entry.id));
        m_eventAthleteTable->setItem(row, 1, item(shown(entry.bibNumber, QStringLiteral("-"))));
        m_eventAthleteTable->setItem(row, 2, item(shown(entry.laneNumber, QStringLiteral("-"))));
        m_eventAthleteTable->setItem(row, 3, item(QString::number(entry.sortOrder)));
        m_eventAthleteTable->setItem(
            row, 4, item(entry.resultScore < 0 ? QStringLiteral("未录入") : QString::number(entry.resultScore)));
        m_eventAthleteTable->setItem(
            row, 5, item(entry.resultRank < 0 ? QStringLiteral("未录入") : QString::number(entry.resultRank)));
    }
    m_eventAthleteCount->setText(QStringLiteral("共 %1 人").arg(m_eventAthletes.size()));
    m_eventAthleteStack->setCurrentIndex(m_eventAthletes.isEmpty() ? 1 : 0);
    int row = eventAthleteRowForId(selectedId);
    if (row < 0 && !m_eventAthletes.isEmpty())
        row = 0;
    if (row >= 0)
    {
        m_eventAthleteTable->setCurrentCell(row, 0);
        selectEventAthlete(row);
    }
    else
        selectEventAthlete(-1);
}

void CompetitionManagementDialog::selectEventAthlete(int row)
{
    m_creatingEventAthlete = false;
    EventAthlete selected;
    if (row >= 0 && row < m_eventAthletes.size())
        selected = m_eventAthletes.at(row);
    selected.eventId = m_currentEventId;
    m_currentEventAthleteId = selected.id;
    setEventAthleteForm(selected);
    updateAvailability();
}

void CompetitionManagementDialog::newCompetition()
{
    {
        QSignalBlocker blocker(m_competitionTable);
        m_competitionTable->setCurrentCell(-1, -1);
    }
    m_currentCompetitionId.clear();
    setCompetitionForm({});
    reloadEvents();
    updateAvailability();
    m_nameEdit->setFocus();
    setStatus({});
    m_detailScroll->verticalScrollBar()->setValue(0);
}

void CompetitionManagementDialog::newEvent()
{
    if (m_currentCompetitionId.isEmpty())
        return;
    {
        QSignalBlocker blocker(m_eventTable);
        m_eventTable->setCurrentCell(-1, -1);
    }
    m_currentEventId.clear();
    m_creatingEvent = true;
    CompetitionEvent event;
    event.competitionId = m_currentCompetitionId;
    setEventForm(event);
    reloadEventAthletes();
    updateAvailability();
    m_raceEdit->setFocus();
    setStatus({});
    m_detailScroll->ensureWidgetVisible(m_eventEditors, 0, 12);
}

void CompetitionManagementDialog::newEventAthlete()
{
    if (m_currentEventId.isEmpty())
        return;
    {
        QSignalBlocker blocker(m_eventAthleteTable);
        m_eventAthleteTable->setCurrentCell(-1, -1);
    }
    m_currentEventAthleteId.clear();
    m_creatingEventAthlete = true;
    EventAthlete entry;
    entry.eventId = m_currentEventId;
    setEventAthleteForm(entry);
    updateAvailability();
    m_athleteCombo->setFocus();
    setStatus({});
    m_detailScroll->ensureWidgetVisible(m_eventAthleteEditors, 0, 12);
}

void CompetitionManagementDialog::setCompetitionForm(const Competition &competition)
{
    m_competitionTitle->setText(competition.id.isEmpty() ? QStringLiteral("新增比赛") : QStringLiteral("比赛资料"));
    m_nameEdit->setText(competition.name);
    m_typeEdit->setText(competition.competitionType);
    m_locationEdit->setText(competition.location);
    m_competitionNotes->setPlainText(competition.notes);
    m_dateEnabled->setChecked(competition.competitionDate.isValid());
    m_dateEdit->setDate(competition.competitionDate.isValid() ? competition.competitionDate : QDate::currentDate());
    m_dateEdit->setEnabled(m_dateEnabled->isChecked());
}

void CompetitionManagementDialog::setEventForm(const CompetitionEvent &event)
{
    m_eventTitle->setText(m_creatingEvent ? QStringLiteral("新增场次") : QStringLiteral("场次详情"));
    m_raceEdit->setText(event.raceName);
    m_eventNameEdit->setText(event.eventName);
    m_groupEdit->setText(event.groupName);
    m_heatEdit->setText(event.heatName);
    m_eventNotes->setPlainText(event.notes);
    m_scheduledEnabled->setChecked(event.scheduledAt.isValid());
    m_scheduledEdit->setDateTime(event.scheduledAt.isValid() ? event.scheduledAt.toLocalTime()
                                                             : QDateTime::currentDateTime());
    m_scheduledEdit->setEnabled(m_scheduledEnabled->isChecked());
}

void CompetitionManagementDialog::populateAthleteChoices()
{
    const QString currentAthleteId =
        m_currentEventAthleteId.isEmpty() ? QString() : m_athleteCombo->currentData().toString();
    QSignalBlocker blocker(m_athleteCombo);
    m_athleteCombo->clear();
    for (const AthleteProfile &athlete : std::as_const(m_athletes))
    {
        bool alreadyPresent = false;
        for (const EventAthlete &entry : std::as_const(m_eventAthletes))
            if (entry.athleteId == athlete.id && entry.id != m_currentEventAthleteId)
            {
                alreadyPresent = true;
                break;
            }
        if (!alreadyPresent)
            m_athleteCombo->addItem(athlete.name, athlete.id);
    }
    const int index = m_athleteCombo->findData(currentAthleteId);
    if (index >= 0)
        m_athleteCombo->setCurrentIndex(index);
}

void CompetitionManagementDialog::setEventAthleteForm(const EventAthlete &entry)
{
    populateAthleteChoices();
    if (!entry.id.isEmpty() && m_athleteCombo->findData(entry.athleteId) < 0)
        m_athleteCombo->addItem(shown(entry.athleteName, entry.athleteId), entry.athleteId);
    const int index = m_athleteCombo->findData(entry.athleteId);
    if (index >= 0)
        m_athleteCombo->setCurrentIndex(index);
    m_athleteCombo->setEnabled(entry.id.isEmpty());
    m_bibEdit->setText(entry.bibNumber);
    m_laneEdit->setText(entry.laneNumber);
    m_sortSpin->setValue(std::max(0, entry.sortOrder));
    m_scoreSpin->setValue(entry.resultScore);
    m_rankSpin->setValue(entry.resultRank);
    m_eventAthleteNotes->setPlainText(entry.notes);
}

void CompetitionManagementDialog::saveCompetition()
{
    if (!m_repository || !m_repository->isOpen())
        return;
    Competition competition;
    for (const Competition &value : std::as_const(m_competitions))
        if (value.id == m_currentCompetitionId)
        {
            competition = value;
            break;
        }
    competition.name = m_nameEdit->text().trimmed();
    competition.competitionType = m_typeEdit->text().trimmed();
    competition.location = m_locationEdit->text().trimmed();
    competition.competitionDate = m_dateEnabled->isChecked() ? m_dateEdit->date() : QDate();
    competition.notes = m_competitionNotes->toPlainText().trimmed();
    competition.active = true;
    if (competition.name.isEmpty())
    {
        QMessageBox::warning(this, QStringLiteral("保存失败"), QStringLiteral("比赛名称不能为空。"));
        return;
    }
    QString error;
    if (!m_repository->saveCompetition(&competition, &error))
    {
        QMessageBox::warning(this, QStringLiteral("保存失败"), error);
        return;
    }
    m_changed = true;
    m_currentCompetitionId = competition.id;
    {
        QSignalBlocker blocker(m_searchEdit);
        m_searchEdit->clear();
    }
    reloadCompetitions(competition.id);
    setStatus(QStringLiteral("比赛已保存。"));
}

void CompetitionManagementDialog::saveEvent()
{
    if (!m_repository || !m_repository->isOpen() || m_currentCompetitionId.isEmpty() ||
        (m_currentEventId.isEmpty() && !m_creatingEvent))
        return;
    CompetitionEvent event;
    for (const CompetitionEvent &value : std::as_const(m_events))
        if (value.id == m_currentEventId)
        {
            event = value;
            break;
        }
    event.competitionId = m_currentCompetitionId;
    event.raceName = m_raceEdit->text().trimmed();
    event.eventName = m_eventNameEdit->text().trimmed();
    event.groupName = m_groupEdit->text().trimmed();
    event.heatName = m_heatEdit->text().trimmed();
    event.scheduledAt = m_scheduledEnabled->isChecked() ? m_scheduledEdit->dateTime() : QDateTime();
    event.notes = m_eventNotes->toPlainText().trimmed();
    event.active = true;
    if (event.raceName.isEmpty() && event.eventName.isEmpty() && event.groupName.isEmpty())
    {
        QMessageBox::warning(this, QStringLiteral("保存失败"),
                             QStringLiteral("场次名称、比赛项目或组别至少填写一项。"));
        return;
    }
    QString error;
    if (!m_repository->saveCompetitionEvent(&event, &error))
    {
        QMessageBox::warning(this, QStringLiteral("保存失败"), error);
        return;
    }
    m_changed = true;
    m_currentEventId = event.id;
    reloadEvents(event.id);
    setStatus(QStringLiteral("场次已保存。"));
}

void CompetitionManagementDialog::saveEventAthlete()
{
    if (!m_repository || !m_repository->isOpen() || m_currentEventId.isEmpty() ||
        (m_currentEventAthleteId.isEmpty() && !m_creatingEventAthlete))
        return;
    EventAthlete entry;
    for (const EventAthlete &value : std::as_const(m_eventAthletes))
        if (value.id == m_currentEventAthleteId)
        {
            entry = value;
            break;
        }
    entry.eventId = m_currentEventId;
    entry.athleteId = m_athleteCombo->currentData().toString();
    entry.athleteName = m_athleteCombo->currentText();
    entry.bibNumber = m_bibEdit->text().trimmed();
    entry.laneNumber = m_laneEdit->text().trimmed();
    entry.sortOrder = m_sortSpin->value();
    entry.resultScore = m_scoreSpin->value();
    entry.resultRank = m_rankSpin->value();
    entry.notes = m_eventAthleteNotes->toPlainText().trimmed();
    entry.active = true;
    if (entry.athleteId.isEmpty())
    {
        QMessageBox::warning(this, QStringLiteral("保存失败"), QStringLiteral("请选择参赛运动员。"));
        return;
    }
    for (const EventAthlete &existing : std::as_const(m_eventAthletes))
        if (existing.athleteId == entry.athleteId && existing.id != entry.id)
        {
            QMessageBox::warning(this, QStringLiteral("保存失败"), QStringLiteral("该运动员已在当前场次中。"));
            return;
        }
    QString error;
    if (!m_repository->saveEventAthlete(&entry, &error))
    {
        QMessageBox::warning(this, QStringLiteral("保存失败"), error);
        return;
    }
    m_changed = true;
    m_currentEventAthleteId = entry.id;
    reloadEventAthletes(entry.id);
    setStatus(QStringLiteral("参赛信息已保存。"));
}

void CompetitionManagementDialog::archiveCompetition()
{
    if (m_currentCompetitionId.isEmpty() || !m_repository || !m_repository->isOpen())
        return;
    if (QMessageBox::question(this, QStringLiteral("归档比赛"),
                              QStringLiteral("归档后该比赛不再出现在新的训练和比赛选择中，\n已有历"
                                             "史记录继续保留。")) != QMessageBox::Yes)
        return;
    QString error;
    if (!m_repository->archiveCompetition(m_currentCompetitionId, &error))
    {
        QMessageBox::warning(this, QStringLiteral("归档失败"), error);
        return;
    }
    m_changed = true;
    m_currentCompetitionId.clear();
    reloadCompetitions();
    setStatus(QStringLiteral("比赛已归档。"));
}

void CompetitionManagementDialog::archiveEvent()
{
    if (m_currentEventId.isEmpty() || !m_repository || !m_repository->isOpen())
        return;
    if (QMessageBox::question(this, QStringLiteral("归档场次"),
                              QStringLiteral("归档后该场次不再出现在新的训练和比赛选择中，\n已有历"
                                             "史记录继续保留。")) != QMessageBox::Yes)
        return;
    QString error;
    if (!m_repository->archiveCompetitionEvent(m_currentEventId, &error))
    {
        QMessageBox::warning(this, QStringLiteral("归档失败"), error);
        return;
    }
    m_changed = true;
    m_currentEventId.clear();
    reloadEvents();
    setStatus(QStringLiteral("场次已归档。"));
}

void CompetitionManagementDialog::removeEventAthlete()
{
    if (m_currentEventAthleteId.isEmpty() || !m_repository || !m_repository->isOpen())
        return;
    if (QMessageBox::question(this, QStringLiteral("移出场次"),
                              QStringLiteral("仅将该运动员移出当前场次，\n不会删"
                                             "除运动员档案和历史训练数据。")) != QMessageBox::Yes)
        return;
    QString error;
    if (!m_repository->archiveEventAthlete(m_currentEventAthleteId, &error))
    {
        QMessageBox::warning(this, QStringLiteral("移出失败"), error);
        return;
    }
    m_changed = true;
    m_currentEventAthleteId.clear();
    reloadEventAthletes();
    setStatus(QStringLiteral("运动员已移出当前场次。"));
}

void CompetitionManagementDialog::updateAvailability()
{
    const bool available = m_repository && m_repository->isOpen();
    const bool hasCompetition = !m_currentCompetitionId.isEmpty();
    const bool hasEvent = !m_currentEventId.isEmpty();
    const bool hasEntry = !m_currentEventAthleteId.isEmpty();
    const bool editEvent = hasCompetition && (hasEvent || m_creatingEvent);
    const bool editEntry = hasEvent && (hasEntry || m_creatingEventAthlete);
    m_serviceWarning->setVisible(!available);
    m_searchEdit->setEnabled(available);
    m_newCompetitionButton->setEnabled(available);
    m_emptyCompetitionButton->setEnabled(available);
    m_competitionEditors->setEnabled(available);
    m_saveCompetitionButton->setEnabled(available);
    m_archiveCompetitionButton->setEnabled(available && hasCompetition);
    m_eventParentHint->setVisible(!hasCompetition);
    m_eventBody->setVisible(hasCompetition);
    m_newEventButton->setEnabled(available && hasCompetition);
    m_emptyEventButton->setEnabled(available && hasCompetition);
    m_eventDetail->setVisible(editEvent);
    m_eventEditors->setEnabled(available && editEvent);
    m_saveEventButton->setEnabled(available && editEvent);
    m_archiveEventButton->setEnabled(available && hasEvent);
    m_athleteParentHint->setVisible(!hasEvent);
    m_athleteBody->setVisible(hasEvent);
    m_newEventAthleteButton->setEnabled(available && hasEvent);
    m_emptyEventAthleteButton->setEnabled(available && hasEvent);
    m_eventAthleteEditors->setVisible(editEntry);
    m_eventAthleteEditors->setEnabled(available && editEntry);
    m_saveEventAthleteButton->setEnabled(available && editEntry && m_athleteCombo->count() > 0);
    m_removeEventAthleteButton->setEnabled(available && hasEntry);
}

void CompetitionManagementDialog::setStatus(const QString &text)
{
    m_statusLabel->setText(text);
    m_statusLabel->setVisible(!text.isEmpty());
}
int CompetitionManagementDialog::competitionRowForId(const QString &id) const
{
    if (id.isEmpty())
        return -1;
    for (int row = 0; row < m_competitionTable->rowCount(); ++row)
        if (m_competitionTable->item(row, 0)->data(Qt::UserRole).toString() == id)
            return row;
    return -1;
}
int CompetitionManagementDialog::eventRowForId(const QString &id) const
{
    if (id.isEmpty())
        return -1;
    for (int row = 0; row < m_events.size(); ++row)
        if (m_events.at(row).id == id)
            return row;
    return -1;
}
int CompetitionManagementDialog::eventAthleteRowForId(const QString &id) const
{
    if (id.isEmpty())
        return -1;
    for (int row = 0; row < m_eventAthletes.size(); ++row)
        if (m_eventAthletes.at(row).id == id)
            return row;
    return -1;
}
