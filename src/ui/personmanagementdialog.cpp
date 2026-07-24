#include "personmanagementdialog.h"

#include "trainingrepository.h"
#include "tensortrtathletebackend.h"

#include <QAbstractItemView>
#include <QCoreApplication>
#include <QDir>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFileDialog>
#include <QFormLayout>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QListWidgetItem>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSignalBlocker>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QTabWidget>
#include <QVBoxLayout>

#include <algorithm>

namespace {

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

QTableWidgetItem *readOnlyItem(const QString &text)
{
    auto *item = new QTableWidgetItem(text);
    item->setFlags(item->flags() & ~Qt::ItemIsEditable);
    return item;
}

QString displayText(const QString &text, const QString &fallback = QStringLiteral("-"))
{
    const QString trimmed = text.trimmed();
    return trimmed.isEmpty() ? fallback : trimmed;
}

void configureTable(QTableWidget *table)
{
    if (!table) {
        return;
    }
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setSelectionMode(QAbstractItemView::SingleSelection);
    table->setAlternatingRowColors(false);
    table->verticalHeader()->setVisible(false);
    table->horizontalHeader()->setStretchLastSection(true);
}

QPushButton *secondaryButton(const QString &text, QWidget *parent)
{
    auto *button = new QPushButton(text, parent);
    button->setProperty("role", "secondaryButton");
    return button;
}

} // namespace

PersonManagementDialog::PersonManagementDialog(TrainingRepository *repository, QWidget *parent)
    : QDialog(parent)
    , m_repository(repository)
{
    setWindowTitle(QStringLiteral("人员管理"));
    resize(1040, 720);
    buildUi();
    reload();
}

bool PersonManagementDialog::changed() const
{
    return m_changed;
}

void PersonManagementDialog::buildUi()
{
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(16, 16, 16, 16);
    root->setSpacing(10);

    auto *title = new QLabel(QStringLiteral("人员管理"), this);
    title->setProperty("role", "sectionTitle");
    root->addWidget(title);

    m_statusLabel = new QLabel(QStringLiteral("档案会保存到本地训练数据库。"), this);
    m_statusLabel->setProperty("role", "muted");
    m_statusLabel->setWordWrap(true);
    root->addWidget(m_statusLabel);

    auto *tabs = new QTabWidget(this);
    tabs->addTab(buildAthletePage(), QStringLiteral("运动员档案"));
    tabs->addTab(buildCoachPage(), QStringLiteral("教练档案"));
    root->addWidget(tabs, 1);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    buttons->button(QDialogButtonBox::Close)->setText(QStringLiteral("关闭"));
    root->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
}

QWidget *PersonManagementDialog::buildAthletePage()
{
    auto *page = new QWidget(this);
    auto *layout = new QHBoxLayout(page);
    layout->setContentsMargins(0, 8, 0, 0);
    layout->setSpacing(12);

    m_athleteTable = new QTableWidget(0, 5, page);
    m_athleteTable->setHorizontalHeaderLabels({
        QStringLiteral("姓名"),
        QStringLiteral("编号"),
        QStringLiteral("组别"),
        QStringLiteral("等级"),
        QStringLiteral("目标")
    });
    configureTable(m_athleteTable);
    m_athleteTable->setMinimumWidth(430);
    layout->addWidget(m_athleteTable, 3);

    auto *formPanel = new QWidget(page);
    auto *formLayout = new QVBoxLayout(formPanel);
    formLayout->setContentsMargins(0, 0, 0, 0);
    formLayout->setSpacing(8);

    auto *form = new QFormLayout();
    form->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);
    form->setHorizontalSpacing(10);
    form->setVerticalSpacing(8);

    m_athleteNameEdit = new QLineEdit(formPanel);
    m_athleteCodeEdit = new QLineEdit(formPanel);
    m_ageGroupEdit = new QLineEdit(formPanel);
    m_heightSpinBox = new QDoubleSpinBox(formPanel);
    m_weightSpinBox = new QDoubleSpinBox(formPanel);
    m_disciplineEdit = new QLineEdit(formPanel);
    m_levelEdit = new QLineEdit(formPanel);
    m_rotationEdit = new QLineEdit(formPanel);
    m_takeoffFootEdit = new QLineEdit(formPanel);
    m_injuryNotesEdit = new QPlainTextEdit(formPanel);
    m_goalsEdit = new QPlainTextEdit(formPanel);

    m_athleteNameEdit->setPlaceholderText(QStringLiteral("姓名"));
    m_athleteCodeEdit->setPlaceholderText(QStringLiteral("ATH-001"));
    m_ageGroupEdit->setPlaceholderText(QStringLiteral("青年组"));
    m_disciplineEdit->setPlaceholderText(QStringLiteral("滑冰"));
    m_levelEdit->setPlaceholderText(QStringLiteral("基础 / 进阶 / 专项"));
    m_rotationEdit->setPlaceholderText(QStringLiteral("顺时针 / 逆时针"));
    m_takeoffFootEdit->setPlaceholderText(QStringLiteral("左脚 / 右脚"));
    m_injuryNotesEdit->setPlaceholderText(QStringLiteral("伤病限制、训练禁忌"));
    m_goalsEdit->setPlaceholderText(QStringLiteral("阶段目标、动作重点"));
    for (auto *editor : {m_injuryNotesEdit, m_goalsEdit}) {
        editor->setMaximumHeight(84);
    }
    m_heightSpinBox->setRange(0.0, 260.0);
    m_heightSpinBox->setDecimals(1);
    m_heightSpinBox->setSuffix(QStringLiteral(" cm"));
    m_weightSpinBox->setRange(0.0, 200.0);
    m_weightSpinBox->setDecimals(1);
    m_weightSpinBox->setSuffix(QStringLiteral(" kg"));

    form->addRow(QStringLiteral("姓名"), m_athleteNameEdit);
    form->addRow(QStringLiteral("编号"), m_athleteCodeEdit);
    form->addRow(QStringLiteral("年龄组"), m_ageGroupEdit);
    form->addRow(QStringLiteral("身高"), m_heightSpinBox);
    form->addRow(QStringLiteral("体重"), m_weightSpinBox);
    form->addRow(QStringLiteral("项目"), m_disciplineEdit);
    form->addRow(QStringLiteral("等级"), m_levelEdit);
    form->addRow(QStringLiteral("惯用旋转"), m_rotationEdit);
    form->addRow(QStringLiteral("起跳脚"), m_takeoffFootEdit);
    form->addRow(QStringLiteral("伤病限制"), m_injuryNotesEdit);
    form->addRow(QStringLiteral("训练目标"), m_goalsEdit);
    formLayout->addLayout(form);

    auto *galleryTitle = new QLabel(QStringLiteral("ReID 样本图片"), formPanel);
    galleryTitle->setProperty("role", "sectionTitle");
    formLayout->addWidget(galleryTitle);
    m_identitySampleTable = new QTableWidget(0, 3, formPanel);
    m_identitySampleTable->setHorizontalHeaderLabels({QStringLiteral("文件"), QStringLiteral("向量维度"), QStringLiteral("版本")});
    configureTable(m_identitySampleTable);
    m_identitySampleTable->setMinimumHeight(110);
    formLayout->addWidget(m_identitySampleTable);

    auto *galleryButtonRow = new QHBoxLayout();
    m_addIdentitySampleButton = secondaryButton(QStringLiteral("添加样本"), formPanel);
    m_deleteIdentitySampleButton = secondaryButton(QStringLiteral("删除样本"), formPanel);
    m_deleteIdentitySampleButton->setProperty("variant", "danger");
    galleryButtonRow->addWidget(m_addIdentitySampleButton);
    galleryButtonRow->addWidget(m_deleteIdentitySampleButton);
    formLayout->addLayout(galleryButtonRow);

    auto *buttonRow = new QHBoxLayout();
    buttonRow->setSpacing(8);
    auto *newButton = secondaryButton(QStringLiteral("新增"), formPanel);
    auto *saveButton = secondaryButton(QStringLiteral("保存"), formPanel);
    auto *deleteButton = secondaryButton(QStringLiteral("删除"), formPanel);
    deleteButton->setProperty("variant", "danger");
    buttonRow->addWidget(newButton);
    buttonRow->addWidget(saveButton);
    buttonRow->addWidget(deleteButton);
    formLayout->addLayout(buttonRow);
    formLayout->addStretch(1);
    layout->addWidget(formPanel, 2);

    connect(m_athleteTable, &QTableWidget::currentCellChanged, this, [this](int row, int, int, int) {
        selectAthleteRow(row);
    });
    connect(newButton, &QPushButton::clicked, this, [this]() { newAthlete(); });
    connect(saveButton, &QPushButton::clicked, this, [this]() { saveAthlete(); });
    connect(deleteButton, &QPushButton::clicked, this, [this]() { archiveAthlete(); });
    connect(m_addIdentitySampleButton, &QPushButton::clicked, this, [this]() { addIdentitySample(); });
    connect(m_deleteIdentitySampleButton, &QPushButton::clicked, this, [this]() { deleteIdentitySample(); });
    return page;
}

QWidget *PersonManagementDialog::buildCoachPage()
{
    auto *page = new QWidget(this);
    auto *layout = new QHBoxLayout(page);
    layout->setContentsMargins(0, 8, 0, 0);
    layout->setSpacing(12);

    m_coachTable = new QTableWidget(0, 5, page);
    m_coachTable->setHorizontalHeaderLabels({
        QStringLiteral("姓名"),
        QStringLiteral("编号"),
        QStringLiteral("专项"),
        QStringLiteral("电话"),
        QStringLiteral("带训人数")
    });
    configureTable(m_coachTable);
    m_coachTable->setMinimumWidth(430);
    layout->addWidget(m_coachTable, 3);

    auto *formPanel = new QWidget(page);
    auto *formLayout = new QVBoxLayout(formPanel);
    formLayout->setContentsMargins(0, 0, 0, 0);
    formLayout->setSpacing(8);

    auto *form = new QFormLayout();
    form->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);
    form->setHorizontalSpacing(10);
    form->setVerticalSpacing(8);

    m_coachNameEdit = new QLineEdit(formPanel);
    m_coachCodeEdit = new QLineEdit(formPanel);
    m_specialtyEdit = new QLineEdit(formPanel);
    m_phoneEdit = new QLineEdit(formPanel);
    m_coachNotesEdit = new QPlainTextEdit(formPanel);
    m_coachAthleteList = new QListWidget(formPanel);

    m_coachNameEdit->setPlaceholderText(QStringLiteral("姓名"));
    m_coachCodeEdit->setPlaceholderText(QStringLiteral("COACH-001"));
    m_specialtyEdit->setPlaceholderText(QStringLiteral("基础滑行 / 跳跃 / 复盘"));
    m_phoneEdit->setPlaceholderText(QStringLiteral("联系电话"));
    m_coachNotesEdit->setPlaceholderText(QStringLiteral("排班、职责、沟通备注"));
    m_coachNotesEdit->setMaximumHeight(84);
    m_coachAthleteList->setMinimumHeight(150);

    form->addRow(QStringLiteral("姓名"), m_coachNameEdit);
    form->addRow(QStringLiteral("编号"), m_coachCodeEdit);
    form->addRow(QStringLiteral("专项"), m_specialtyEdit);
    form->addRow(QStringLiteral("电话"), m_phoneEdit);
    form->addRow(QStringLiteral("备注"), m_coachNotesEdit);
    form->addRow(QStringLiteral("带训运动员"), m_coachAthleteList);
    formLayout->addLayout(form);

    auto *buttonRow = new QHBoxLayout();
    buttonRow->setSpacing(8);
    auto *newButton = secondaryButton(QStringLiteral("新增"), formPanel);
    auto *saveButton = secondaryButton(QStringLiteral("保存"), formPanel);
    auto *deleteButton = secondaryButton(QStringLiteral("删除"), formPanel);
    deleteButton->setProperty("variant", "danger");
    buttonRow->addWidget(newButton);
    buttonRow->addWidget(saveButton);
    buttonRow->addWidget(deleteButton);
    formLayout->addLayout(buttonRow);
    formLayout->addStretch(1);
    layout->addWidget(formPanel, 2);

    connect(m_coachTable, &QTableWidget::currentCellChanged, this, [this](int row, int, int, int) {
        selectCoachRow(row);
    });
    connect(newButton, &QPushButton::clicked, this, [this]() { newCoach(); });
    connect(saveButton, &QPushButton::clicked, this, [this]() { saveCoach(); });
    connect(deleteButton, &QPushButton::clicked, this, [this]() { archiveCoach(); });
    return page;
}

void PersonManagementDialog::reload()
{
    if (!m_repository || !m_repository->isOpen()) {
        setStatus(QStringLiteral("训练数据库未就绪。"));
        return;
    }

    m_athletes = m_repository->athletes();
    m_coaches = m_repository->coaches();
    populateAthleteTable();
    populateCoachTable();

    const int athleteRow = athleteRowForId(m_currentAthleteId);
    if (athleteRow >= 0) {
        m_athleteTable->selectRow(athleteRow);
        selectAthleteRow(athleteRow);
    } else if (!m_athletes.isEmpty()) {
        m_athleteTable->selectRow(0);
        selectAthleteRow(0);
    } else {
        newAthlete();
    }

    const int coachRow = coachRowForId(m_currentCoachId);
    if (coachRow >= 0) {
        m_coachTable->selectRow(coachRow);
        selectCoachRow(coachRow);
    } else if (!m_coaches.isEmpty()) {
        m_coachTable->selectRow(0);
        selectCoachRow(0);
    } else {
        newCoach();
    }
}

void PersonManagementDialog::populateAthleteTable()
{
    QSignalBlocker blocker(m_athleteTable);
    m_athleteTable->setRowCount(m_athletes.size());
    for (int row = 0; row < m_athletes.size(); ++row) {
        const AthleteProfile &athlete = m_athletes.at(row);
        m_athleteTable->setItem(row, 0, readOnlyItem(athlete.name));
        m_athleteTable->setItem(row, 1, readOnlyItem(displayText(athlete.code)));
        m_athleteTable->setItem(row, 2, readOnlyItem(displayText(athlete.ageGroup)));
        m_athleteTable->setItem(row, 3, readOnlyItem(displayText(athlete.level)));
        m_athleteTable->setItem(row, 4, readOnlyItem(displayText(athlete.goals)));
        m_athleteTable->item(row, 0)->setData(Qt::UserRole, athlete.id);
    }
    m_athleteTable->resizeColumnsToContents();
    m_athleteTable->horizontalHeader()->setStretchLastSection(true);
}

void PersonManagementDialog::populateCoachTable()
{
    QSignalBlocker blocker(m_coachTable);
    m_coachTable->setRowCount(m_coaches.size());
    for (int row = 0; row < m_coaches.size(); ++row) {
        const CoachProfile &coach = m_coaches.at(row);
        const int athleteCount = m_repository ? m_repository->athleteIdsForCoach(coach.id).size() : 0;
        m_coachTable->setItem(row, 0, readOnlyItem(coach.name));
        m_coachTable->setItem(row, 1, readOnlyItem(displayText(coach.code)));
        m_coachTable->setItem(row, 2, readOnlyItem(displayText(coach.specialty)));
        m_coachTable->setItem(row, 3, readOnlyItem(displayText(coach.phone)));
        m_coachTable->setItem(row, 4, readOnlyItem(QString::number(athleteCount)));
        m_coachTable->item(row, 0)->setData(Qt::UserRole, coach.id);
    }
    m_coachTable->resizeColumnsToContents();
    m_coachTable->horizontalHeader()->setStretchLastSection(true);
}

void PersonManagementDialog::populateCoachAthleteList(const QVector<QString> &checkedAthleteIds)
{
    QSignalBlocker blocker(m_coachAthleteList);
    m_coachAthleteList->clear();
    for (const AthleteProfile &athlete : std::as_const(m_athletes)) {
        auto *item = new QListWidgetItem(QStringLiteral("%1 · %2").arg(athlete.name, displayText(athlete.code)),
                                         m_coachAthleteList);
        item->setData(Qt::UserRole, athlete.id);
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(checkedAthleteIds.contains(athlete.id) ? Qt::Checked : Qt::Unchecked);
    }
}

void PersonManagementDialog::selectAthleteRow(int row)
{
    if (row < 0 || row >= m_athletes.size()) {
        return;
    }
    setAthleteForm(m_athletes.at(row));
    populateIdentitySamples();
}

void PersonManagementDialog::selectCoachRow(int row)
{
    if (row < 0 || row >= m_coaches.size()) {
        return;
    }
    const CoachProfile &coach = m_coaches.at(row);
    setCoachForm(coach);
    populateCoachAthleteList(m_repository ? m_repository->athleteIdsForCoach(coach.id) : QVector<QString>());
}

void PersonManagementDialog::newAthlete()
{
    m_currentAthleteId.clear();
    AthleteProfile athlete;
    athlete.discipline = QStringLiteral("滑冰");
    athlete.level = QStringLiteral("基础");
    athlete.ageGroup = QStringLiteral("未分组");
    setAthleteForm(athlete);
    populateIdentitySamples();
    if (m_athleteTable) {
        m_athleteTable->clearSelection();
    }
    setStatus(QStringLiteral("正在新增运动员档案。"));
}

void PersonManagementDialog::saveAthlete()
{
    if (!m_repository || !m_repository->isOpen()) {
        setStatus(QStringLiteral("训练数据库未就绪。"));
        return;
    }

    AthleteProfile athlete = athleteFromForm();
    QString errorMessage;
    if (!m_repository->saveAthleteProfile(&athlete, &errorMessage)) {
        QMessageBox::warning(this, QStringLiteral("保存失败"), errorMessage);
        return;
    }
    m_currentAthleteId = athlete.id;
    m_changed = true;
    reload();
    setStatus(QStringLiteral("运动员档案已保存：%1").arg(athlete.name));
}

void PersonManagementDialog::populateIdentitySamples()
{
    if (!m_identitySampleTable) {
        return;
    }
    const QVector<AthleteIdentitySample> samples = m_currentAthleteId.isEmpty()
                                                       ? QVector<AthleteIdentitySample>()
                                                       : m_repository->identitySamples(m_currentAthleteId);
    QSignalBlocker blocker(m_identitySampleTable);
    m_identitySampleTable->setRowCount(samples.size());
    for (int row = 0; row < samples.size(); ++row) {
        const AthleteIdentitySample &sample = samples.at(row);
        m_identitySampleTable->setItem(row, 0, readOnlyItem(displayText(sample.fileName)));
        m_identitySampleTable->setItem(row, 1, readOnlyItem(QString::number(sample.embeddingDimension)));
        m_identitySampleTable->setItem(row, 2, readOnlyItem(QStringLiteral("%1 / %2")
                                                                  .arg(displayText(sample.modelVersion),
                                                                       displayText(sample.preprocessingVersion))));
        m_identitySampleTable->item(row, 0)->setData(Qt::UserRole, sample.id);
    }
    m_identitySampleTable->resizeColumnsToContents();
}

void PersonManagementDialog::addIdentitySample()
{
    if (m_currentAthleteId.isEmpty() || !m_repository || !m_repository->isOpen()) {
        setStatus(QStringLiteral("请先保存运动员档案，再添加 ReID 样本。"));
        return;
    }
    const QString filePath = QFileDialog::getOpenFileName(this,
                                                          QStringLiteral("选择运动员样本图片"),
                                                          QString(),
                                                          QStringLiteral("图片 (*.jpg *.jpeg *.png *.webp *.bmp)"));
    if (filePath.isEmpty()) {
        return;
    }
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
    if (!m_identityBackend) {
        m_identityBackend = std::make_unique<TensorRtAthleteBackend>();
    }
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
    if (!m_repository->uploadIdentitySample(m_currentAthleteId,
                                            QFileInfo(filePath).fileName(),
                                            imageData,
                                            QString::fromLatin1(kIdentityModelVersion),
                                            QString::fromLatin1(kIdentityPreprocessingVersion),
                                            &sample,
                                            &errorMessage)
        || !m_repository->saveIdentityEmbedding(m_currentAthleteId,
                                                sample.id,
                                                embedding,
                                                QString::fromLatin1(kIdentityModelVersion),
                                                QString::fromLatin1(kIdentityPreprocessingVersion),
                                                &errorMessage)) {
        QMessageBox::warning(this, QStringLiteral("添加失败"), errorMessage);
        return;
    }
    m_changed = true;
    populateIdentitySamples();
    setStatus(QStringLiteral("ReID 样本已添加。"));
}

void PersonManagementDialog::deleteIdentitySample()
{
    if (!m_identitySampleTable || m_currentAthleteId.isEmpty()) {
        return;
    }
    const int row = m_identitySampleTable->currentRow();
    if (row < 0 || !m_identitySampleTable->item(row, 0)) {
        setStatus(QStringLiteral("请先选择要删除的样本。"));
        return;
    }
    if (QMessageBox::question(this, QStringLiteral("删除样本"), QStringLiteral("确定删除当前 ReID 样本吗？")) != QMessageBox::Yes) {
        return;
    }
    QString errorMessage;
    if (!m_repository->deleteIdentitySample(m_currentAthleteId,
                                            m_identitySampleTable->item(row, 0)->data(Qt::UserRole).toString(),
                                            &errorMessage)) {
        QMessageBox::warning(this, QStringLiteral("删除失败"), errorMessage);
        return;
    }
    m_changed = true;
    populateIdentitySamples();
    setStatus(QStringLiteral("ReID 样本已删除。"));
}

void PersonManagementDialog::archiveAthlete()
{
    if (m_currentAthleteId.trimmed().isEmpty()) {
        QMessageBox::information(this, QStringLiteral("未选择运动员"), QStringLiteral("请先选择要删除的运动员。"));
        return;
    }
    if (QMessageBox::question(this,
                              QStringLiteral("删除运动员"),
                              QStringLiteral("删除后该运动员将不再出现在训练选择中，历史记录仍会保留。确定删除吗？"))
        != QMessageBox::Yes) {
        return;
    }

    QString errorMessage;
    if (!m_repository->archiveAthlete(m_currentAthleteId, &errorMessage)) {
        QMessageBox::warning(this, QStringLiteral("删除失败"), errorMessage);
        return;
    }
    m_currentAthleteId.clear();
    m_changed = true;
    reload();
    setStatus(QStringLiteral("运动员已删除。"));
}

void PersonManagementDialog::newCoach()
{
    m_currentCoachId.clear();
    CoachProfile coach;
    setCoachForm(coach);
    populateCoachAthleteList();
    if (m_coachTable) {
        m_coachTable->clearSelection();
    }
    setStatus(QStringLiteral("正在新增教练档案。"));
}

void PersonManagementDialog::saveCoach()
{
    if (!m_repository || !m_repository->isOpen()) {
        setStatus(QStringLiteral("训练数据库未就绪。"));
        return;
    }

    CoachProfile coach = coachFromForm();
    QString errorMessage;
    if (!m_repository->saveCoachProfile(&coach, selectedCoachAthleteIds(), &errorMessage)) {
        QMessageBox::warning(this, QStringLiteral("保存失败"), errorMessage);
        return;
    }
    m_currentCoachId = coach.id;
    m_changed = true;
    reload();
    setStatus(QStringLiteral("教练档案已保存：%1").arg(coach.name));
}

void PersonManagementDialog::archiveCoach()
{
    if (m_currentCoachId.trimmed().isEmpty()) {
        QMessageBox::information(this, QStringLiteral("未选择教练"), QStringLiteral("请先选择要删除的教练。"));
        return;
    }
    if (QMessageBox::question(this,
                              QStringLiteral("删除教练"),
                              QStringLiteral("删除后该教练将不再出现在训练选择中，历史记录仍会保留。确定删除吗？"))
        != QMessageBox::Yes) {
        return;
    }

    QString errorMessage;
    if (!m_repository->archiveCoach(m_currentCoachId, &errorMessage)) {
        QMessageBox::warning(this, QStringLiteral("删除失败"), errorMessage);
        return;
    }
    m_currentCoachId.clear();
    m_changed = true;
    reload();
    setStatus(QStringLiteral("教练已删除。"));
}

void PersonManagementDialog::setAthleteForm(const AthleteProfile &athlete)
{
    m_currentAthleteId = athlete.id;
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
}

void PersonManagementDialog::setCoachForm(const CoachProfile &coach)
{
    m_currentCoachId = coach.id;
    m_coachNameEdit->setText(coach.name);
    m_coachCodeEdit->setText(coach.code);
    m_specialtyEdit->setText(coach.specialty);
    m_phoneEdit->setText(coach.phone);
    m_coachNotesEdit->setPlainText(coach.notes);
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
    for (int i = 0; i < m_coachAthleteList->count(); ++i) {
        const QListWidgetItem *item = m_coachAthleteList->item(i);
        if (item && item->checkState() == Qt::Checked) {
            result.append(item->data(Qt::UserRole).toString());
        }
    }
    return result;
}

int PersonManagementDialog::athleteRowForId(const QString &athleteId) const
{
    for (int i = 0; i < m_athletes.size(); ++i) {
        if (m_athletes.at(i).id == athleteId) {
            return i;
        }
    }
    return -1;
}

int PersonManagementDialog::coachRowForId(const QString &coachId) const
{
    for (int i = 0; i < m_coaches.size(); ++i) {
        if (m_coaches.at(i).id == coachId) {
            return i;
        }
    }
    return -1;
}

void PersonManagementDialog::setStatus(const QString &text)
{
    if (m_statusLabel) {
        m_statusLabel->setText(text);
    }
}
