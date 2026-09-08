#include "personmanagementdialog.h"

#include "animatedbutton.h"
#include "trainingrepository.h"

#include <QAbstractItemView>
#include <QApplication>
#include <QButtonGroup>
#include <QCheckBox>
#include <QCoreApplication>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFrame>
#include <QGridLayout>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QImage>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QListWidgetItem>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPainter>
#include <QPushButton>
#include <QScreen>
#include <QScrollArea>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QSizePolicy>
#include <QStackedWidget>
#include <QStyledItemDelegate>
#include <QStyleOptionButton>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>

#include <algorithm>

namespace {

class CoachAthleteDelegate final : public QStyledItemDelegate
{
public:
    explicit CoachAthleteDelegate(QListWidget *list)
        : QStyledItemDelegate(list)
        , m_styleSource(new QCheckBox(list))
    {
        m_styleSource->hide();
    }

    void paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const override
    {
        QStyledItemDelegate::paint(painter, option, index);
        if (!(index.flags() & Qt::ItemIsUserCheckable)) return;

        QStyleOptionViewItem viewOption(option);
        initStyleOption(&viewOption, index);
        QRect indicatorRect = m_styleSource->style()->subElementRect(
            QStyle::SE_ItemViewItemCheckIndicator, &viewOption, viewOption.widget);
        const int width = m_styleSource->style()->pixelMetric(QStyle::PM_IndicatorWidth, nullptr, m_styleSource);
        const int height = m_styleSource->style()->pixelMetric(QStyle::PM_IndicatorHeight, nullptr, m_styleSource);
        indicatorRect = QRect(indicatorRect.center().x() - width / 2,
                              indicatorRect.center().y() - height / 2,
                              width,
                              height);

        QStyleOptionButton checkOption;
        checkOption.initFrom(m_styleSource);
        checkOption.rect = indicatorRect;
        checkOption.state |= index.data(Qt::CheckStateRole).toInt() == Qt::Checked
                                 ? QStyle::State_On
                                 : QStyle::State_Off;
        if (!(index.flags() & Qt::ItemIsEnabled)) checkOption.state &= ~QStyle::State_Enabled;
        m_styleSource->style()->drawPrimitive(QStyle::PE_IndicatorCheckBox,
                                              &checkOption,
                                              painter,
                                              m_styleSource);
    }

private:
    QCheckBox *m_styleSource = nullptr;
};

constexpr const char *kIdentityModelVersion = "personvit-msmt17-vit-base-v1";
constexpr const char *kIdentityPreprocessingVersion = "rgb-256x128-mean0.5-std0.5-l2-v1";

QString identityModelDir()
{
    const QDir appDir(QCoreApplication::applicationDirPath());
    const QString deployed = appDir.absoluteFilePath(QStringLiteral("models/athlete"));
    return QDir(deployed).exists()
               ? deployed
               : QDir(QCoreApplication::applicationDirPath()).absoluteFilePath(QStringLiteral("../../models/athlete"));
}

QString displayText(const QString &text, const QString &fallback = QStringLiteral("-"))
{
    const QString trimmed = text.trimmed();
    return trimmed.isEmpty() ? fallback : trimmed;
}

QTableWidgetItem *readOnlyItem(const QString &text, const QString &toolTip = {})
{
    auto *item = new QTableWidgetItem(text);
    item->setFlags(item->flags() & ~Qt::ItemIsEditable);
    item->setToolTip(toolTip.isEmpty() ? text : toolTip);
    return item;
}

void configureTable(QTableWidget *table)
{
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setSelectionMode(QAbstractItemView::SingleSelection);
    table->setAlternatingRowColors(false);
    table->setShowGrid(false);
    table->setMouseTracking(true);
    table->setAttribute(Qt::WA_Hover, true);
    table->verticalHeader()->setVisible(false);
    table->verticalHeader()->setDefaultSectionSize(42);
    table->horizontalHeader()->setHighlightSections(false);
    table->horizontalHeader()->setSectionsClickable(false);
    table->setTextElideMode(Qt::ElideRight);
    table->setWordWrap(false);
}

AnimatedButton *makeButton(const QString &text, const QString &variant, QWidget *parent)
{
    auto *button = new AnimatedButton(parent);
    button->setText(text);
    button->setProperty("variant", variant);
    button->setMinimumHeight(36);
    return button;
}

QLabel *makeSectionTitle(const QString &text, QWidget *parent)
{
    auto *label = new QLabel(text, parent);
    label->setObjectName(QStringLiteral("sectionTitle"));
    return label;
}

void addField(QGridLayout *grid, int row, int column, const QString &labelText, QWidget *editor)
{
    auto *field = new QWidget(editor->parentWidget());
    auto *layout = new QVBoxLayout(field);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(6);
    auto *label = new QLabel(labelText, field);
    label->setObjectName(QStringLiteral("settingLabel"));
    layout->addWidget(label);
    layout->addWidget(editor);
    grid->addWidget(field, row, column);
}

void addInlineField(QGridLayout *grid, int row, int column, const QString &labelText, QWidget *editor)
{
    auto *field = new QWidget(editor->parentWidget());
    auto *layout = new QHBoxLayout(field);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(8);
    auto *label = new QLabel(labelText, field);
    label->setObjectName(QStringLiteral("settingLabel"));
    label->setFixedWidth(68);
    layout->addWidget(label);
    layout->addWidget(editor, 1);
    grid->addWidget(field, row, column);
}

QWidget *makeEmptyState(const QString &title,
                        const QString &description,
                        QPushButton **actionButton,
                        const QString &actionText,
                        QWidget *parent)
{
    auto *empty = new QWidget(parent);
    auto *layout = new QVBoxLayout(empty);
    layout->setContentsMargins(24, 24, 24, 24);
    layout->setSpacing(10);
    layout->addStretch();
    auto *titleLabel = new QLabel(title, empty);
    titleLabel->setObjectName(QStringLiteral("sectionTitle"));
    titleLabel->setAlignment(Qt::AlignCenter);
    layout->addWidget(titleLabel);
    auto *descriptionLabel = new QLabel(description, empty);
    descriptionLabel->setObjectName(QStringLiteral("pageDescription"));
    descriptionLabel->setWordWrap(true);
    descriptionLabel->setAlignment(Qt::AlignCenter);
    layout->addWidget(descriptionLabel);
    auto *button = makeButton(actionText, QStringLiteral("subtle"), empty);
    layout->addWidget(button, 0, Qt::AlignHCenter);
    layout->addStretch();
    *actionButton = button;
    return empty;
}

QWidget *makeNoMatchState(QWidget *parent)
{
    auto *state = new QWidget(parent);
    auto *layout = new QVBoxLayout(state);
    auto *label = new QLabel(QStringLiteral("没有匹配的人员档案"), state);
    label->setObjectName(QStringLiteral("pageDescription"));
    label->setAlignment(Qt::AlignCenter);
    layout->addWidget(label);
    return state;
}

void refreshButtonStyle(QPushButton *button)
{
    button->style()->unpolish(button);
    button->style()->polish(button);
    button->update();
}

} // namespace

PersonManagementDialog::PersonManagementDialog(TrainingRepository *repository, QWidget *parent)
    : FramelessDialog(parent)
    , m_repository(repository)
{
    setDialogTitle(QStringLiteral("人员管理"));
    setMinimumSize(1000, 680);
    QSize initialSize(1180, 760);
    if (const QScreen *screen = QApplication::primaryScreen()) {
        const QSize availableSize = screen->availableGeometry().size() - QSize(24, 24);
        initialSize.setWidth(std::max(1000, std::min(initialSize.width(), availableSize.width())));
        initialSize.setHeight(std::max(680, std::min(initialSize.height(), availableSize.height())));
    }
    resize(initialSize);
    setSizeGripEnabled(true);
    if (auto *titleBar = findChild<QWidget *>(QStringLiteral("dialogTitleBar"))) {
        titleBar->setFixedHeight(48);
    }
    buildUi();
    ensurePolished();
    // Global input minimum heights are resolved during polish; apply compact dialog heights afterward.
    m_identitySampleTable->setFixedHeight(160);
    m_injuryNotesEdit->setFixedHeight(80);
    m_goalsEdit->setFixedHeight(80);
    m_coachNotesEdit->setFixedHeight(84);
    reload();
    setStatus({});
}

bool PersonManagementDialog::changed() const
{
    return m_changed;
}

void PersonManagementDialog::buildUi()
{
    auto *root = contentLayout();
    root->setContentsMargins(18, 14, 18, 16);
    root->setSpacing(10);

    auto *description = new QLabel(QStringLiteral("维护运动员、教练及身份识别资料"), this);
    description->setObjectName(QStringLiteral("pageDescription"));
    root->addWidget(description);

    m_serviceWarningLabel = new QLabel(QStringLiteral("训练服务未连接，暂时无法管理人员档案。"), this);
    m_serviceWarningLabel->setObjectName(QStringLiteral("actionStatus"));
    m_serviceWarningLabel->setWordWrap(true);
    root->addWidget(m_serviceWarningLabel);

    auto *switchRow = new QHBoxLayout();
    switchRow->setSpacing(6);
    auto *athleteButton = makeButton(QStringLiteral("运动员"), QStringLiteral("primary"), this);
    auto *coachButton = makeButton(QStringLiteral("教练"), QStringLiteral("subtle"), this);
    athleteButton->setCheckable(true);
    coachButton->setCheckable(true);
    athleteButton->setChecked(true);
    athleteButton->setProperty("active", true);
    coachButton->setProperty("active", false);
    athleteButton->setMinimumWidth(112);
    coachButton->setMinimumWidth(112);
    auto *group = new QButtonGroup(this);
    group->setExclusive(true);
    group->addButton(athleteButton, 0);
    group->addButton(coachButton, 1);
    switchRow->addWidget(athleteButton);
    switchRow->addWidget(coachButton);
    switchRow->addStretch();
    root->addLayout(switchRow);

    m_pages = new QStackedWidget(this);
    m_pages->addWidget(buildAthletePage());
    m_pages->addWidget(buildCoachPage());
    root->addWidget(m_pages, 1);

    m_statusLabel = new QLabel(this);
    m_statusLabel->setObjectName(QStringLiteral("actionStatus"));
    m_statusLabel->setMinimumHeight(18);
    root->addWidget(m_statusLabel);

    connect(group, &QButtonGroup::idClicked, this, [this, athleteButton, coachButton](int id) {
        m_pages->setCurrentIndex(id);
        athleteButton->setProperty("variant", id == 0 ? "primary" : "subtle");
        coachButton->setProperty("variant", id == 1 ? "primary" : "subtle");
        athleteButton->setProperty("active", id == 0);
        coachButton->setProperty("active", id == 1);
        refreshButtonStyle(athleteButton);
        refreshButtonStyle(coachButton);
    });
}

QWidget *PersonManagementDialog::buildAthletePage()
{
    auto *page = new QWidget(this);
    page->setObjectName(QStringLiteral("settingsBody"));
    auto *pageLayout = new QVBoxLayout(page);
    pageLayout->setContentsMargins(0, 0, 0, 0);
    pageLayout->setSpacing(10);

    auto *toolbar = new QHBoxLayout();
    toolbar->setSpacing(10);
    m_athleteSearchEdit = new QLineEdit(page);
    m_athleteSearchEdit->setPlaceholderText(QStringLiteral("搜索姓名或编号"));
    m_athleteSearchEdit->setClearButtonEnabled(true);
    m_athleteSearchEdit->setMaximumWidth(360);
    toolbar->addWidget(m_athleteSearchEdit, 1);
    m_athleteCountLabel = new QLabel(page);
    m_athleteCountLabel->setObjectName(QStringLiteral("pageDescription"));
    toolbar->addWidget(m_athleteCountLabel);
    toolbar->addStretch();
    m_newAthleteButton = makeButton(QStringLiteral("新增运动员"), QStringLiteral("subtle"), page);
    toolbar->addWidget(m_newAthleteButton);
    pageLayout->addLayout(toolbar);

    auto *workspace = new QWidget(page);
    auto *workspaceLayout = new QHBoxLayout(workspace);
    workspaceLayout->setContentsMargins(0, 0, 0, 0);
    workspaceLayout->setSpacing(18);

    m_athleteListStack = new QStackedWidget(workspace);
    m_athleteTable = new QTableWidget(0, 4, m_athleteListStack);
    m_athleteTable->setHorizontalHeaderLabels({QStringLiteral("姓名"), QStringLiteral("编号"), QStringLiteral("年龄组"), QStringLiteral("等级")});
    configureTable(m_athleteTable);
    m_athleteTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    m_athleteListStack->addWidget(m_athleteTable);
    m_athleteListStack->addWidget(makeEmptyState(QStringLiteral("暂无运动员档案"),
                                                 QStringLiteral("创建第一位运动员后，\n即可用于实时训练和身份识别。"),
                                                 &m_emptyNewAthleteButton,
                                                 QStringLiteral("新增运动员"),
                                                 m_athleteListStack));
    m_athleteListStack->addWidget(makeNoMatchState(m_athleteListStack));
    workspaceLayout->addWidget(m_athleteListStack, 37);

    auto *detailPanel = new QWidget(workspace);
    auto *detailLayout = new QVBoxLayout(detailPanel);
    detailLayout->setContentsMargins(0, 0, 0, 0);
    detailLayout->setSpacing(10);
    m_athleteDetailTitle = new QLabel(QStringLiteral("运动员资料"), detailPanel);
    m_athleteDetailTitle->setObjectName(QStringLiteral("pageTitle"));
    detailLayout->addWidget(m_athleteDetailTitle);

    auto *scroll = new QScrollArea(detailPanel);
    m_athleteDetailScroll = scroll;
    scroll->setObjectName(QStringLiteral("settingsPage"));
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_athleteDetailContent = new QWidget(scroll);
    auto *formLayout = new QVBoxLayout(m_athleteDetailContent);
    formLayout->setContentsMargins(0, 4, 10, 8);
    formLayout->setSpacing(12);

    formLayout->addWidget(makeSectionTitle(QStringLiteral("基本资料"), m_athleteDetailContent));
    auto *basicGrid = new QGridLayout();
    basicGrid->setHorizontalSpacing(18);
    basicGrid->setVerticalSpacing(9);
    basicGrid->setColumnStretch(0, 1);
    basicGrid->setColumnStretch(1, 1);
    m_athleteNameEdit = new QLineEdit(m_athleteDetailContent);
    m_athleteCodeEdit = new QLineEdit(m_athleteDetailContent);
    m_ageGroupEdit = new QLineEdit(m_athleteDetailContent);
    m_disciplineEdit = new QLineEdit(m_athleteDetailContent);
    m_heightSpinBox = new QDoubleSpinBox(m_athleteDetailContent);
    m_weightSpinBox = new QDoubleSpinBox(m_athleteDetailContent);
    m_levelEdit = new QLineEdit(m_athleteDetailContent);
    m_rotationEdit = new QLineEdit(m_athleteDetailContent);
    m_takeoffFootEdit = new QLineEdit(m_athleteDetailContent);
    m_athleteNameEdit->setPlaceholderText(QStringLiteral("姓名"));
    m_athleteCodeEdit->setPlaceholderText(QStringLiteral("ATH-001"));
    m_ageGroupEdit->setPlaceholderText(QStringLiteral("青年组"));
    m_disciplineEdit->setPlaceholderText(QStringLiteral("滑冰"));
    m_levelEdit->setPlaceholderText(QStringLiteral("基础 / 进阶 / 专项"));
    m_rotationEdit->setPlaceholderText(QStringLiteral("顺时针 / 逆时针"));
    m_takeoffFootEdit->setPlaceholderText(QStringLiteral("左脚 / 右脚"));
    m_heightSpinBox->setRange(0.0, 260.0);
    m_heightSpinBox->setDecimals(1);
    m_heightSpinBox->setSuffix(QStringLiteral(" cm"));
    m_weightSpinBox->setRange(0.0, 200.0);
    m_weightSpinBox->setDecimals(1);
    m_weightSpinBox->setSuffix(QStringLiteral(" kg"));
    addInlineField(basicGrid, 0, 0, QStringLiteral("姓名"), m_athleteNameEdit);
    addInlineField(basicGrid, 0, 1, QStringLiteral("编号"), m_athleteCodeEdit);
    addInlineField(basicGrid, 1, 0, QStringLiteral("年龄组"), m_ageGroupEdit);
    addInlineField(basicGrid, 1, 1, QStringLiteral("项目"), m_disciplineEdit);
    addInlineField(basicGrid, 2, 0, QStringLiteral("身高"), m_heightSpinBox);
    addInlineField(basicGrid, 2, 1, QStringLiteral("体重"), m_weightSpinBox);
    addInlineField(basicGrid, 3, 0, QStringLiteral("等级"), m_levelEdit);
    addInlineField(basicGrid, 3, 1, QStringLiteral("惯用旋转"), m_rotationEdit);
    addInlineField(basicGrid, 4, 0, QStringLiteral("起跳脚"), m_takeoffFootEdit);
    formLayout->addLayout(basicGrid);
    formLayout->addSpacing(6);

    formLayout->addWidget(makeSectionTitle(QStringLiteral("训练信息"), m_athleteDetailContent));
    auto *trainingGrid = new QGridLayout();
    trainingGrid->setVerticalSpacing(12);
    m_injuryNotesEdit = new QPlainTextEdit(m_athleteDetailContent);
    m_goalsEdit = new QPlainTextEdit(m_athleteDetailContent);
    m_injuryNotesEdit->setPlaceholderText(QStringLiteral("伤病限制、训练禁忌"));
    m_goalsEdit->setPlaceholderText(QStringLiteral("阶段目标、动作重点"));
    m_injuryNotesEdit->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_goalsEdit->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    addField(trainingGrid, 0, 0, QStringLiteral("伤病限制"), m_injuryNotesEdit);
    addField(trainingGrid, 1, 0, QStringLiteral("训练目标"), m_goalsEdit);
    formLayout->addLayout(trainingGrid);
    formLayout->addSpacing(6);

    auto *identityHeader = new QHBoxLayout();
    identityHeader->addWidget(makeSectionTitle(QStringLiteral("身份识别资料"), m_athleteDetailContent));
    identityHeader->addStretch();
    m_identitySampleCountLabel = new QLabel(QStringLiteral("0 个样本"), m_athleteDetailContent);
    m_identitySampleCountLabel->setObjectName(QStringLiteral("pageDescription"));
    identityHeader->addWidget(m_identitySampleCountLabel);
    formLayout->addLayout(identityHeader);
    m_identitySampleTable = new QTableWidget(0, 3, m_athleteDetailContent);
    m_identitySampleTable->setHorizontalHeaderLabels({QStringLiteral("样本文件"), QStringLiteral("向量"), QStringLiteral("模型版本")});
    configureTable(m_identitySampleTable);
    m_identitySampleTable->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_identitySampleTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_identitySampleTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_identitySampleTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    formLayout->addWidget(m_identitySampleTable);
    auto *identityActions = new QHBoxLayout();
    m_addIdentitySampleButton = makeButton(QStringLiteral("添加样本"), QStringLiteral("subtle"), m_athleteDetailContent);
    m_deleteIdentitySampleButton = makeButton(QStringLiteral("删除样本"), QStringLiteral("danger"), m_athleteDetailContent);
    identityActions->addWidget(m_addIdentitySampleButton);
    identityActions->addWidget(m_deleteIdentitySampleButton);
    identityActions->addStretch();
    formLayout->addLayout(identityActions);
    formLayout->addStretch();
    scroll->setWidget(m_athleteDetailContent);
    detailLayout->addWidget(scroll, 1);

    auto *actions = new QHBoxLayout();
    m_archiveAthleteButton = makeButton(QStringLiteral("归档运动员"), QStringLiteral("danger"), detailPanel);
    m_saveAthleteButton = makeButton(QStringLiteral("保存"), QStringLiteral("primary"), detailPanel);
    actions->addWidget(m_archiveAthleteButton);
    actions->addStretch();
    actions->addWidget(m_saveAthleteButton);
    detailLayout->addSpacing(4);
    detailLayout->addLayout(actions);
    workspaceLayout->addWidget(detailPanel, 63);
    pageLayout->addWidget(workspace, 1);

    connect(m_athleteSearchEdit, &QLineEdit::textChanged, this, [this]() { applyAthleteFilter(); });
    connect(m_athleteTable, &QTableWidget::currentCellChanged, this, [this](int row, int, int, int) { selectAthleteRow(row); });
    connect(m_newAthleteButton, &QPushButton::clicked, this, [this]() { newAthlete(); });
    connect(m_emptyNewAthleteButton, &QPushButton::clicked, this, [this]() { newAthlete(); });
    connect(m_saveAthleteButton, &QPushButton::clicked, this, [this]() { saveAthlete(); });
    connect(m_archiveAthleteButton, &QPushButton::clicked, this, [this]() { archiveAthlete(); });
    connect(m_addIdentitySampleButton, &QPushButton::clicked, this, [this]() { addIdentitySample(); });
    connect(m_deleteIdentitySampleButton, &QPushButton::clicked, this, [this]() { deleteIdentitySample(); });
    connect(m_identitySampleTable, &QTableWidget::itemSelectionChanged, this, [this]() { updateIdentityActions(); });
    connect(m_identitySampleTable, &QTableWidget::itemSelectionChanged, this, [this]() { updateIdentityActions(); });
    return page;
}

QWidget *PersonManagementDialog::buildCoachPage()
{
    auto *page = new QWidget(this);
    page->setObjectName(QStringLiteral("settingsBody"));
    auto *pageLayout = new QVBoxLayout(page);
    pageLayout->setContentsMargins(0, 0, 0, 0);
    pageLayout->setSpacing(10);

    auto *toolbar = new QHBoxLayout();
    toolbar->setSpacing(10);
    m_coachSearchEdit = new QLineEdit(page);
    m_coachSearchEdit->setPlaceholderText(QStringLiteral("搜索姓名或编号"));
    m_coachSearchEdit->setClearButtonEnabled(true);
    m_coachSearchEdit->setMaximumWidth(360);
    toolbar->addWidget(m_coachSearchEdit, 1);
    m_coachCountLabel = new QLabel(page);
    m_coachCountLabel->setObjectName(QStringLiteral("pageDescription"));
    toolbar->addWidget(m_coachCountLabel);
    toolbar->addStretch();
    m_newCoachButton = makeButton(QStringLiteral("新增教练"), QStringLiteral("subtle"), page);
    toolbar->addWidget(m_newCoachButton);
    pageLayout->addLayout(toolbar);

    auto *workspace = new QWidget(page);
    auto *workspaceLayout = new QHBoxLayout(workspace);
    workspaceLayout->setContentsMargins(0, 0, 0, 0);
    workspaceLayout->setSpacing(18);
    m_coachListStack = new QStackedWidget(workspace);
    m_coachTable = new QTableWidget(0, 5, m_coachListStack);
    m_coachTable->setHorizontalHeaderLabels({QStringLiteral("姓名"), QStringLiteral("编号"), QStringLiteral("专项"), QStringLiteral("电话"), QStringLiteral("带训人数")});
    configureTable(m_coachTable);
    m_coachTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    m_coachTable->horizontalHeader()->setSectionResizeMode(4, QHeaderView::ResizeToContents);
    m_coachTable->horizontalHeaderItem(4)->setToolTip(QStringLiteral("带训人数"));
    m_coachListStack->addWidget(m_coachTable);
    m_coachListStack->addWidget(makeEmptyState(QStringLiteral("暂无教练档案"), QString(), &m_emptyNewCoachButton,
                                               QStringLiteral("新增教练"), m_coachListStack));
    m_coachListStack->addWidget(makeNoMatchState(m_coachListStack));
    workspaceLayout->addWidget(m_coachListStack, 37);

    auto *detailPanel = new QWidget(workspace);
    auto *detailLayout = new QVBoxLayout(detailPanel);
    detailLayout->setContentsMargins(0, 0, 0, 0);
    detailLayout->setSpacing(10);
    m_coachDetailTitle = new QLabel(QStringLiteral("教练资料"), detailPanel);
    m_coachDetailTitle->setObjectName(QStringLiteral("pageTitle"));
    detailLayout->addWidget(m_coachDetailTitle);
    auto *scroll = new QScrollArea(detailPanel);
    m_coachDetailScroll = scroll;
    scroll->setObjectName(QStringLiteral("settingsPage"));
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_coachDetailContent = new QWidget(scroll);
    auto *formLayout = new QVBoxLayout(m_coachDetailContent);
    formLayout->setContentsMargins(0, 4, 10, 8);
    formLayout->setSpacing(12);
    formLayout->addWidget(makeSectionTitle(QStringLiteral("基本资料"), m_coachDetailContent));
    auto *basicGrid = new QGridLayout();
    basicGrid->setHorizontalSpacing(18);
    basicGrid->setVerticalSpacing(9);
    basicGrid->setColumnStretch(0, 1);
    basicGrid->setColumnStretch(1, 1);
    m_coachNameEdit = new QLineEdit(m_coachDetailContent);
    m_coachCodeEdit = new QLineEdit(m_coachDetailContent);
    m_specialtyEdit = new QLineEdit(m_coachDetailContent);
    m_phoneEdit = new QLineEdit(m_coachDetailContent);
    m_coachNameEdit->setPlaceholderText(QStringLiteral("姓名"));
    m_coachCodeEdit->setPlaceholderText(QStringLiteral("COACH-001"));
    m_specialtyEdit->setPlaceholderText(QStringLiteral("基础滑行 / 跳跃 / 复盘"));
    m_phoneEdit->setPlaceholderText(QStringLiteral("联系电话"));
    addInlineField(basicGrid, 0, 0, QStringLiteral("姓名"), m_coachNameEdit);
    addInlineField(basicGrid, 0, 1, QStringLiteral("编号"), m_coachCodeEdit);
    addInlineField(basicGrid, 1, 0, QStringLiteral("专项"), m_specialtyEdit);
    addInlineField(basicGrid, 1, 1, QStringLiteral("电话"), m_phoneEdit);
    formLayout->addLayout(basicGrid);
    m_coachNotesEdit = new QPlainTextEdit(m_coachDetailContent);
    m_coachNotesEdit->setPlaceholderText(QStringLiteral("排班、职责、沟通备注"));
    m_coachNotesEdit->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    auto *notesGrid = new QGridLayout();
    addField(notesGrid, 0, 0, QStringLiteral("备注"), m_coachNotesEdit);
    formLayout->addLayout(notesGrid);
    formLayout->addSpacing(6);

    auto *relationHeader = new QHBoxLayout();
    relationHeader->addWidget(makeSectionTitle(QStringLiteral("带训运动员"), m_coachDetailContent));
    relationHeader->addStretch();
    m_coachAthleteCountLabel = new QLabel(QStringLiteral("已选择 0 人"), m_coachDetailContent);
    m_coachAthleteCountLabel->setObjectName(QStringLiteral("pageDescription"));
    relationHeader->addWidget(m_coachAthleteCountLabel);
    formLayout->addLayout(relationHeader);
    m_coachAthleteList = new QListWidget(m_coachDetailContent);
    m_coachAthleteList->setMinimumHeight(210);
    m_coachAthleteList->setMouseTracking(true);
    m_coachAthleteList->setAttribute(Qt::WA_Hover, true);
    m_coachAthleteList->setItemDelegate(new CoachAthleteDelegate(m_coachAthleteList));
    formLayout->addWidget(m_coachAthleteList);
    formLayout->addStretch();
    scroll->setWidget(m_coachDetailContent);
    detailLayout->addWidget(scroll, 1);
    detailLayout->addSpacing(4);
    auto *actions = new QHBoxLayout();
    m_archiveCoachButton = makeButton(QStringLiteral("归档教练"), QStringLiteral("danger"), detailPanel);
    m_saveCoachButton = makeButton(QStringLiteral("保存"), QStringLiteral("primary"), detailPanel);
    actions->addWidget(m_archiveCoachButton);
    actions->addStretch();
    actions->addWidget(m_saveCoachButton);
    detailLayout->addLayout(actions);
    workspaceLayout->addWidget(detailPanel, 63);
    pageLayout->addWidget(workspace, 1);

    connect(m_coachSearchEdit, &QLineEdit::textChanged, this, [this]() { applyCoachFilter(); });
    connect(m_coachTable, &QTableWidget::currentCellChanged, this, [this](int row, int, int, int) { selectCoachRow(row); });
    connect(m_newCoachButton, &QPushButton::clicked, this, [this]() { newCoach(); });
    connect(m_emptyNewCoachButton, &QPushButton::clicked, this, [this]() { newCoach(); });
    connect(m_saveCoachButton, &QPushButton::clicked, this, [this]() { saveCoach(); });
    connect(m_archiveCoachButton, &QPushButton::clicked, this, [this]() { archiveCoach(); });
    connect(m_coachAthleteList, &QListWidget::itemChanged, this, [this]() { updateCoachAthleteCount(); });
    return page;
}

void PersonManagementDialog::reload()
{
    const bool available = m_repository && m_repository->isOpen();
    if (available) {
        m_athletes = m_repository->athletes();
        m_coaches = m_repository->coaches();
    } else {
        m_athletes.clear();
        m_coaches.clear();
    }
    populateAthleteTable();
    populateCoachTable();
    updateRepositoryAvailability();
    if (!available) {
        m_currentAthleteId.clear();
        m_currentCoachId.clear();
        newAthlete();
        newCoach();
        setStatus({});
        return;
    }

    int athleteRow = athleteRowForId(m_currentAthleteId);
    if (athleteRow < 0) {
        for (int row = 0; row < m_athletes.size(); ++row) {
            if (!m_athleteTable->isRowHidden(row)) {
                athleteRow = row;
                break;
            }
        }
    }
    if (athleteRow >= 0) {
        if (!m_athleteTable->isRowHidden(athleteRow)) m_athleteTable->selectRow(athleteRow);
        selectAthleteRow(athleteRow);
    } else {
        newAthlete();
    }

    int coachRow = coachRowForId(m_currentCoachId);
    if (coachRow < 0) {
        for (int row = 0; row < m_coaches.size(); ++row) {
            if (!m_coachTable->isRowHidden(row)) {
                coachRow = row;
                break;
            }
        }
    }
    if (coachRow >= 0) {
        if (!m_coachTable->isRowHidden(coachRow)) m_coachTable->selectRow(coachRow);
        selectCoachRow(coachRow);
    } else {
        newCoach();
    }
}

void PersonManagementDialog::populateAthleteTable()
{
    QSignalBlocker blocker(m_athleteTable);
    m_athleteTable->clearSelection();
    m_athleteTable->setCurrentCell(-1, -1);
    m_athleteTable->setRowCount(m_athletes.size());
    for (int row = 0; row < m_athletes.size(); ++row) {
        const AthleteProfile &athlete = m_athletes.at(row);
        const QStringList values{athlete.name, displayText(athlete.code), displayText(athlete.ageGroup), displayText(athlete.level)};
        for (int column = 0; column < values.size(); ++column) {
            m_athleteTable->setItem(row, column, readOnlyItem(values.at(column)));
        }
        m_athleteTable->item(row, 0)->setData(Qt::UserRole, athlete.id);
    }
    applyAthleteFilter();
}

void PersonManagementDialog::populateCoachTable()
{
    QSignalBlocker blocker(m_coachTable);
    m_coachTable->clearSelection();
    m_coachTable->setCurrentCell(-1, -1);
    m_coachTable->setRowCount(m_coaches.size());
    for (int row = 0; row < m_coaches.size(); ++row) {
        const CoachProfile &coach = m_coaches.at(row);
        const int count = m_repository && m_repository->isOpen() ? m_repository->athleteIdsForCoach(coach.id).size() : 0;
        const QStringList values{coach.name, displayText(coach.code), displayText(coach.specialty), displayText(coach.phone), QString::number(count)};
        for (int column = 0; column < values.size(); ++column) {
            m_coachTable->setItem(row, column, readOnlyItem(values.at(column)));
        }
        m_coachTable->item(row, 0)->setData(Qt::UserRole, coach.id);
    }
    applyCoachFilter();
}

void PersonManagementDialog::applyAthleteFilter()
{
    if (!m_athleteTable) return;
    const QString query = m_athleteSearchEdit->text().trimmed();
    int visible = 0;
    for (int row = 0; row < m_athletes.size(); ++row) {
        const AthleteProfile &athlete = m_athletes.at(row);
        const bool match = query.isEmpty() || athlete.name.contains(query, Qt::CaseInsensitive)
                           || athlete.code.contains(query, Qt::CaseInsensitive);
        m_athleteTable->setRowHidden(row, !match);
        visible += match ? 1 : 0;
    }
    m_athleteCountLabel->setText(query.isEmpty() ? QStringLiteral("共 %1 人").arg(m_athletes.size())
                                                  : QStringLiteral("%1 / %2").arg(visible).arg(m_athletes.size()));
    m_athleteListStack->setCurrentIndex(m_athletes.isEmpty() ? 1 : (visible == 0 ? 2 : 0));
}

void PersonManagementDialog::applyCoachFilter()
{
    if (!m_coachTable) return;
    const QString query = m_coachSearchEdit->text().trimmed();
    int visible = 0;
    for (int row = 0; row < m_coaches.size(); ++row) {
        const CoachProfile &coach = m_coaches.at(row);
        const bool match = query.isEmpty() || coach.name.contains(query, Qt::CaseInsensitive)
                           || coach.code.contains(query, Qt::CaseInsensitive)
                           || coach.specialty.contains(query, Qt::CaseInsensitive);
        m_coachTable->setRowHidden(row, !match);
        visible += match ? 1 : 0;
    }
    m_coachCountLabel->setText(query.isEmpty() ? QStringLiteral("共 %1 人").arg(m_coaches.size())
                                                : QStringLiteral("%1 / %2").arg(visible).arg(m_coaches.size()));
    m_coachListStack->setCurrentIndex(m_coaches.isEmpty() ? 1 : (visible == 0 ? 2 : 0));
}

void PersonManagementDialog::updateRepositoryAvailability()
{
    const bool available = m_repository && m_repository->isOpen();
    m_serviceWarningLabel->setVisible(!available);
    for (QWidget *widget : {m_athleteDetailContent, m_coachDetailContent}) widget->setEnabled(available);
    for (QPushButton *button : {m_newAthleteButton, m_emptyNewAthleteButton, m_saveAthleteButton,
                                m_newCoachButton, m_emptyNewCoachButton, m_saveCoachButton}) button->setEnabled(available);
    m_archiveAthleteButton->setEnabled(available && !m_currentAthleteId.isEmpty());
    m_archiveCoachButton->setEnabled(available && !m_currentCoachId.isEmpty());
    m_athleteSearchEdit->setEnabled(available);
    m_coachSearchEdit->setEnabled(available);
    updateIdentityActions();
}

void PersonManagementDialog::populateCoachAthleteList(const QVector<QString> &checkedAthleteIds)
{
    QSignalBlocker blocker(m_coachAthleteList);
    m_coachAthleteList->clear();
    for (const AthleteProfile &athlete : std::as_const(m_athletes)) {
        auto *item = new QListWidgetItem(QStringLiteral("%1    %2").arg(athlete.name, displayText(athlete.code)), m_coachAthleteList);
        item->setData(Qt::UserRole, athlete.id);
        item->setToolTip(QStringLiteral("%1 · %2").arg(athlete.name, displayText(athlete.code)));
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(checkedAthleteIds.contains(athlete.id) ? Qt::Checked : Qt::Unchecked);
        item->setSizeHint(QSize(0, 36));
    }
    updateCoachAthleteCount();
}

void PersonManagementDialog::updateCoachAthleteCount()
{
    if (m_coachAthleteCountLabel) {
        m_coachAthleteCountLabel->setText(QStringLiteral("已选择 %1 人").arg(selectedCoachAthleteIds().size()));
    }
}

void PersonManagementDialog::selectAthleteRow(int row)
{
    if (row < 0 || row >= m_athletes.size()) return;
    setAthleteForm(m_athletes.at(row));
    populateIdentitySamples();
    setStatus({});
}

void PersonManagementDialog::selectCoachRow(int row)
{
    if (row < 0 || row >= m_coaches.size()) return;
    const CoachProfile &coach = m_coaches.at(row);
    setCoachForm(coach);
    populateCoachAthleteList(m_repository && m_repository->isOpen()
                                 ? m_repository->athleteIdsForCoach(coach.id)
                                 : QVector<QString>());
    setStatus({});
}

void PersonManagementDialog::newAthlete()
{
    if (m_athleteTable) {
        QSignalBlocker blocker(m_athleteTable);
        m_athleteTable->clearSelection();
        m_athleteTable->setCurrentCell(-1, -1);
    }
    AthleteProfile athlete;
    athlete.discipline = QStringLiteral("滑冰");
    athlete.level = QStringLiteral("基础");
    athlete.ageGroup = QStringLiteral("未分组");
    setAthleteForm(athlete);
    populateIdentitySamples();
    if (m_athleteDetailScroll) m_athleteDetailScroll->verticalScrollBar()->setValue(0);
    setStatus(QStringLiteral("正在新增运动员档案"));
}

void PersonManagementDialog::newCoach()
{
    if (m_coachTable) {
        QSignalBlocker blocker(m_coachTable);
        m_coachTable->clearSelection();
        m_coachTable->setCurrentCell(-1, -1);
    }
    setCoachForm(CoachProfile());
    populateCoachAthleteList();
    if (m_coachDetailScroll) m_coachDetailScroll->verticalScrollBar()->setValue(0);
    setStatus(QStringLiteral("正在新增教练档案"));
}

void PersonManagementDialog::saveAthlete()
{
    if (!m_repository || !m_repository->isOpen()) return;
    AthleteProfile athlete = athleteFromForm();
    QString errorMessage;
    if (!m_repository->saveAthleteProfile(&athlete, &errorMessage)) {
        QMessageBox::warning(this, QStringLiteral("保存失败"), errorMessage);
        return;
    }
    m_currentAthleteId = athlete.id;
    m_changed = true;
    reload();
    setStatus(QStringLiteral("运动员档案已保存"));
}

void PersonManagementDialog::saveCoach()
{
    if (!m_repository || !m_repository->isOpen()) return;
    CoachProfile coach = coachFromForm();
    QString errorMessage;
    if (!m_repository->saveCoachProfile(&coach, selectedCoachAthleteIds(), &errorMessage)) {
        QMessageBox::warning(this, QStringLiteral("保存失败"), errorMessage);
        return;
    }
    m_currentCoachId = coach.id;
    m_changed = true;
    reload();
    setStatus(QStringLiteral("教练档案已保存"));
}

void PersonManagementDialog::archiveAthlete()
{
    if (!m_repository || !m_repository->isOpen()) return;
    if (m_currentAthleteId.trimmed().isEmpty()) {
        QMessageBox::information(this, QStringLiteral("未选择运动员"), QStringLiteral("请先选择要归档的运动员。"));
        return;
    }
    if (QMessageBox::question(this, QStringLiteral("归档运动员"),
                              QStringLiteral("归档后不再出现在训练选择中，历史记录仍保留。\n\n确定归档当前运动员吗？"))
        != QMessageBox::Yes) return;
    QString errorMessage;
    if (!m_repository->archiveAthlete(m_currentAthleteId, &errorMessage)) {
        QMessageBox::warning(this, QStringLiteral("归档失败"), errorMessage);
        return;
    }
    m_currentAthleteId.clear();
    m_changed = true;
    reload();
    setStatus(QStringLiteral("运动员已归档"));
}

void PersonManagementDialog::archiveCoach()
{
    if (!m_repository || !m_repository->isOpen()) return;
    if (m_currentCoachId.trimmed().isEmpty()) {
        QMessageBox::information(this, QStringLiteral("未选择教练"), QStringLiteral("请先选择要归档的教练。"));
        return;
    }
    if (QMessageBox::question(this, QStringLiteral("归档教练"),
                              QStringLiteral("归档后不再出现在训练选择中，历史记录仍保留。\n\n确定归档当前教练吗？"))
        != QMessageBox::Yes) return;
    QString errorMessage;
    if (!m_repository->archiveCoach(m_currentCoachId, &errorMessage)) {
        QMessageBox::warning(this, QStringLiteral("归档失败"), errorMessage);
        return;
    }
    m_currentCoachId.clear();
    m_changed = true;
    reload();
    setStatus(QStringLiteral("教练已归档"));
}

void PersonManagementDialog::populateIdentitySamples()
{
    if (!m_identitySampleTable) return;
    const QVector<AthleteIdentitySample> samples = m_currentAthleteId.isEmpty() || !m_repository || !m_repository->isOpen()
                                                       ? QVector<AthleteIdentitySample>()
                                                       : m_repository->identitySamples(m_currentAthleteId);
    QSignalBlocker blocker(m_identitySampleTable);
    m_identitySampleTable->clearSelection();
    m_identitySampleTable->setCurrentCell(-1, -1);
    m_identitySampleTable->setRowCount(samples.size());
    for (int row = 0; row < samples.size(); ++row) {
        const AthleteIdentitySample &sample = samples.at(row);
        const QString version = QStringLiteral("%1 / %2").arg(displayText(sample.modelVersion), displayText(sample.preprocessingVersion));
        m_identitySampleTable->setItem(row, 0, readOnlyItem(displayText(sample.fileName)));
        m_identitySampleTable->setItem(row, 1, readOnlyItem(QString::number(sample.embeddingDimension)));
        m_identitySampleTable->setItem(row, 2, readOnlyItem(version, version));
        m_identitySampleTable->item(row, 0)->setData(Qt::UserRole, sample.id);
    }
    m_identitySampleCountLabel->setText(QStringLiteral("%1 个样本").arg(samples.size()));
    updateIdentityActions();
}

void PersonManagementDialog::updateIdentityActions()
{
    if (!m_addIdentitySampleButton || !m_deleteIdentitySampleButton) return;
    const bool available = m_repository && m_repository->isOpen();
    const bool savedAthlete = !m_currentAthleteId.isEmpty();
    m_addIdentitySampleButton->setEnabled(available && savedAthlete);
    m_addIdentitySampleButton->setToolTip(savedAthlete ? QString() : QStringLiteral("请先保存运动员档案"));
    const int row = m_identitySampleTable ? m_identitySampleTable->currentRow() : -1;
    const bool actualSelection = row >= 0 && m_identitySampleTable->item(row, 0)
                                 && m_identitySampleTable->selectionModel()->isRowSelected(row, QModelIndex());
    m_deleteIdentitySampleButton->setEnabled(available && savedAthlete && actualSelection);
}

void PersonManagementDialog::addIdentitySample()
{
    if (m_currentAthleteId.isEmpty() || !m_repository || !m_repository->isOpen()) return;
    const QString filePath = QFileDialog::getOpenFileName(this, QStringLiteral("选择运动员样本图片"), QString(),
                                                          QStringLiteral("图片 (*.jpg *.jpeg *.png *.webp *.bmp)"));
    if (filePath.isEmpty()) return;
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        QMessageBox::warning(this, QStringLiteral("添加失败"), QStringLiteral("无法读取样本图片。"));
        return;
    }
    const QByteArray imageData = file.readAll();
    const QImage image = QImage::fromData(imageData);
    if (image.isNull()) {
        QMessageBox::warning(this, QStringLiteral("添加失败"), QStringLiteral("样本文件不是有效图片。"));
        return;
    }
    if (!m_identityBackend) m_identityBackend = std::make_unique<TensorRtAthleteBackend>();
    if (!m_identityBackendInitialized) {
        QString initError;
        if (!m_identityBackend->initialize(identityModelDir(), &initError) || !m_identityBackend->hasReid()) {
            QMessageBox::warning(this, QStringLiteral("添加失败"), QStringLiteral("PersonViT 未就绪：%1").arg(initError));
            return;
        }
        m_identityBackendInitialized = true;
    }
    QString embeddingError;
    const QVector<float> embedding = m_identityBackend->extractEmbedding(image, &embeddingError);
    if (embedding.isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("添加失败"), embeddingError);
        return;
    }
    AthleteIdentitySample sample;
    QString errorMessage;
    if (!m_repository->uploadIdentitySample(m_currentAthleteId, QFileInfo(filePath).fileName(), imageData,
                                            QString::fromLatin1(kIdentityModelVersion),
                                            QString::fromLatin1(kIdentityPreprocessingVersion), &sample, &errorMessage)
        || !m_repository->saveIdentityEmbedding(m_currentAthleteId, sample.id, embedding,
                                                QString::fromLatin1(kIdentityModelVersion),
                                                QString::fromLatin1(kIdentityPreprocessingVersion), &errorMessage)) {
        QMessageBox::warning(this, QStringLiteral("添加失败"), errorMessage);
        return;
    }
    m_changed = true;
    populateIdentitySamples();
    setStatus(QStringLiteral("ReID 样本已添加"));
}

void PersonManagementDialog::deleteIdentitySample()
{
    if (!m_repository || !m_repository->isOpen() || !m_identitySampleTable || m_currentAthleteId.isEmpty()) return;
    const int row = m_identitySampleTable->currentRow();
    if (row < 0 || !m_identitySampleTable->item(row, 0)) return;
    if (QMessageBox::question(this, QStringLiteral("删除样本"), QStringLiteral("确定删除当前 ReID 样本吗？")) != QMessageBox::Yes) return;
    QString errorMessage;
    if (!m_repository->deleteIdentitySample(m_currentAthleteId,
                                            m_identitySampleTable->item(row, 0)->data(Qt::UserRole).toString(),
                                            &errorMessage)) {
        QMessageBox::warning(this, QStringLiteral("删除失败"), errorMessage);
        return;
    }
    m_changed = true;
    populateIdentitySamples();
    setStatus(QStringLiteral("ReID 样本已删除"));
}

void PersonManagementDialog::setAthleteForm(const AthleteProfile &athlete)
{
    m_currentAthleteId = athlete.id;
    m_athleteDetailTitle->setText(athlete.id.isEmpty() ? QStringLiteral("新增运动员") : QStringLiteral("运动员资料"));
    m_athleteNameEdit->setText(athlete.name);
    m_athleteCodeEdit->setText(athlete.code);
    m_ageGroupEdit->setText(athlete.ageGroup);
    m_heightSpinBox->setValue(std::clamp(athlete.heightCm, 0.0, 260.0));
    m_weightSpinBox->setValue(std::clamp(athlete.weightKg, 0.0, 200.0));
    m_disciplineEdit->setText(athlete.discipline);
    m_levelEdit->setText(athlete.level);
    m_rotationEdit->setText(athlete.preferredRotation);
    m_takeoffFootEdit->setText(athlete.preferredTakeoffFoot);
    m_injuryNotesEdit->setPlainText(athlete.injuryNotes);
    m_goalsEdit->setPlainText(athlete.goals);
    if (m_archiveAthleteButton) {
        m_archiveAthleteButton->setEnabled(m_repository && m_repository->isOpen() && !athlete.id.isEmpty());
    }
    updateIdentityActions();
}

void PersonManagementDialog::setCoachForm(const CoachProfile &coach)
{
    m_currentCoachId = coach.id;
    m_coachDetailTitle->setText(coach.id.isEmpty() ? QStringLiteral("新增教练") : QStringLiteral("教练资料"));
    m_coachNameEdit->setText(coach.name);
    m_coachCodeEdit->setText(coach.code);
    m_specialtyEdit->setText(coach.specialty);
    m_phoneEdit->setText(coach.phone);
    m_coachNotesEdit->setPlainText(coach.notes);
    if (m_archiveCoachButton) {
        m_archiveCoachButton->setEnabled(m_repository && m_repository->isOpen() && !coach.id.isEmpty());
    }
}

AthleteProfile PersonManagementDialog::athleteFromForm() const
{
    AthleteProfile athlete;
    athlete.id = m_currentAthleteId;
    athlete.name = m_athleteNameEdit->text().trimmed();
    athlete.code = m_athleteCodeEdit->text().trimmed();
    athlete.ageGroup = m_ageGroupEdit->text().trimmed();
    athlete.heightCm = m_heightSpinBox->value();
    athlete.weightKg = m_weightSpinBox->value();
    athlete.discipline = m_disciplineEdit->text().trimmed();
    athlete.level = m_levelEdit->text().trimmed();
    athlete.preferredRotation = m_rotationEdit->text().trimmed();
    athlete.preferredTakeoffFoot = m_takeoffFootEdit->text().trimmed();
    athlete.injuryNotes = m_injuryNotesEdit->toPlainText().trimmed();
    athlete.goals = m_goalsEdit->toPlainText().trimmed();
    athlete.active = true;
    return athlete;
}

CoachProfile PersonManagementDialog::coachFromForm() const
{
    CoachProfile coach;
    coach.id = m_currentCoachId;
    coach.name = m_coachNameEdit->text().trimmed();
    coach.code = m_coachCodeEdit->text().trimmed();
    coach.specialty = m_specialtyEdit->text().trimmed();
    coach.phone = m_phoneEdit->text().trimmed();
    coach.notes = m_coachNotesEdit->toPlainText().trimmed();
    coach.active = true;
    return coach;
}

QVector<QString> PersonManagementDialog::selectedCoachAthleteIds() const
{
    QVector<QString> result;
    if (!m_coachAthleteList) return result;
    for (int row = 0; row < m_coachAthleteList->count(); ++row) {
        const QListWidgetItem *item = m_coachAthleteList->item(row);
        if (item && item->checkState() == Qt::Checked) result.append(item->data(Qt::UserRole).toString());
    }
    return result;
}

int PersonManagementDialog::athleteRowForId(const QString &athleteId) const
{
    for (int row = 0; row < m_athletes.size(); ++row) {
        if (m_athletes.at(row).id == athleteId) return row;
    }
    return -1;
}

int PersonManagementDialog::coachRowForId(const QString &coachId) const
{
    for (int row = 0; row < m_coaches.size(); ++row) {
        if (m_coaches.at(row).id == coachId) return row;
    }
    return -1;
}

void PersonManagementDialog::setStatus(const QString &text)
{
    if (m_statusLabel) m_statusLabel->setText(text);
}
