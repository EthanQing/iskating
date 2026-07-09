#include "systemsettingsdialog.h"

#include "cameraconfigtemplate.h"
#include "cameraconnectivitytester.h"

#include <QComboBox>
#include <QAbstractItemView>
#include <QCheckBox>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QGridLayout>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QIntValidator>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QSpinBox>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QThread>
#include <QVBoxLayout>

#include <algorithm>

namespace {

QString normalizeStoredPath(const QString &path)
{
    QString normalized = path.trimmed();
    while (normalized.startsWith(QLatin1Char('/'))) {
        normalized.remove(0, 1);
    }
    return normalized;
}

QString precisionLabel(const QString &value)
{
    if (value == QStringLiteral("fast")) {
        return QStringLiteral("快速 Fast");
    }
    if (value == QStringLiteral("high")) {
        return QStringLiteral("高精度 High");
    }
    return QStringLiteral("平衡 Balanced");
}

CameraSlotSettings defaultCameraSlotSettings(int cameraIndex)
{
    CameraSlotSettings settings;
    const double segmentLengthM = 5.0;
    settings.fieldStartM = cameraIndex * segmentLengthM;
    settings.fieldEndM = (cameraIndex + 1) * segmentLengthM;
    settings.role = QStringLiteral("轨迹分段");
    settings.trajectoryEnabled = true;
    return settings;
}

void populateFpsOptions(QComboBox *comboBox)
{
    if (!comboBox) {
        return;
    }

    for (int fps : {5, 8, 10, 15, 25, 30, 50, 60, 90, 120}) {
        comboBox->addItem(QStringLiteral("%1 FPS").arg(fps), fps);
    }
}

void populateStreamCountOptions(QComboBox *comboBox, int cameraCount)
{
    if (!comboBox) {
        return;
    }

    const int maxStreams = std::max(1, cameraCount);
    for (int count = 1; count <= maxStreams; ++count) {
        comboBox->addItem(QStringLiteral("%1 路").arg(count), count);
    }
}

QTableWidgetItem *makeTableItem(const QString &text)
{
    auto *item = new QTableWidgetItem(text);
    item->setTextAlignment(Qt::AlignCenter);
    return item;
}

QTableWidgetItem *makeEnabledItem(bool enabled)
{
    auto *item = makeTableItem(QString());
    item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
    item->setCheckState(enabled ? Qt::Checked : Qt::Unchecked);
    return item;
}

double tableDoubleValue(const QTableWidget *table, int row, int column, double fallback)
{
    if (!table) {
        return fallback;
    }
    const QTableWidgetItem *item = table->item(row, column);
    if (!item) {
        return fallback;
    }
    bool ok = false;
    const double value = item->text().trimmed().toDouble(&ok);
    return ok ? value : fallback;
}

QString tableTextValue(const QTableWidget *table, int row, int column)
{
    if (!table) {
        return {};
    }
    const QTableWidgetItem *item = table->item(row, column);
    return item ? item->text().trimmed() : QString();
}

} // namespace

SystemSettingsDialog::SystemSettingsDialog(int cameraCount, QWidget *parent)
    : FramelessDialog(parent)
    , m_cameraCount(cameraCount)
{
    setWindowTitle(QStringLiteral("系统设置"));
    setDialogTitle(windowTitle());
    setMinimumWidth(640);

    auto *layout = contentLayout();

    auto *tipLabel = new QLabel(QStringLiteral("统一设置公共 RTSP 参数；每一路相机仅需填写 IP。"), this);
    tipLabel->setWordWrap(true);
    layout->addWidget(tipLabel);

    auto *templateButtonLayout = new QHBoxLayout();
    templateButtonLayout->addStretch();
    auto *importTemplateButton = new QPushButton(QStringLiteral("导入模板"), this);
    auto *exportTemplateButton = new QPushButton(QStringLiteral("导出模板"), this);
    m_connectivityTestButton = new QPushButton(QStringLiteral("连通测试"), this);
    importTemplateButton->setToolTip(QStringLiteral("从 JSON 文件导入公共 RTSP 参数和 12 路相机配置。"));
    exportTemplateButton->setToolTip(QStringLiteral("把当前公共 RTSP 参数和 12 路相机配置导出为 JSON 模板。"));
    m_connectivityTestButton->setToolTip(QStringLiteral("按当前表单配置逐路测试 RTSP 预览流，结果不会自动保存。"));
    templateButtonLayout->addWidget(importTemplateButton);
    templateButtonLayout->addWidget(exportTemplateButton);
    templateButtonLayout->addWidget(m_connectivityTestButton);
    layout->addLayout(templateButtonLayout);

    auto *sharedTitle = new QLabel(QStringLiteral("公共 RTSP 配置"), this);
    layout->addWidget(sharedTitle);

    auto *sharedForm = new QFormLayout();
    sharedForm->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);
    sharedForm->setFormAlignment(Qt::AlignTop);
    sharedForm->setHorizontalSpacing(12);
    sharedForm->setVerticalSpacing(10);

    m_usernameEdit = new QLineEdit(this);
    m_usernameEdit->setClearButtonEnabled(true);
    m_usernameEdit->setPlaceholderText(QStringLiteral("例如：admin"));
    sharedForm->addRow(QStringLiteral("账号"), m_usernameEdit);

    m_passwordEdit = new QLineEdit(this);
    m_passwordEdit->setEchoMode(QLineEdit::Password);
    m_passwordEdit->setClearButtonEnabled(true);
    m_passwordEdit->setPlaceholderText(QStringLiteral("可留空"));
    sharedForm->addRow(QStringLiteral("密码"), m_passwordEdit);

    m_portEdit = new QLineEdit(this);
    m_portEdit->setClearButtonEnabled(true);
    m_portEdit->setPlaceholderText(QStringLiteral("554"));
    m_portEdit->setValidator(new QIntValidator(1, 65535, m_portEdit));
    sharedForm->addRow(QStringLiteral("端口"), m_portEdit);

    m_previewPathEdit = new QLineEdit(this);
    m_previewPathEdit->setClearButtonEnabled(true);
    m_previewPathEdit->setPlaceholderText(QStringLiteral("例如：Streaming/Channels/102"));
    sharedForm->addRow(QStringLiteral("预览路径"), m_previewPathEdit);

    m_previewFpsComboBox = new QComboBox(this);
    populateFpsOptions(m_previewFpsComboBox);
    sharedForm->addRow(QStringLiteral("预览 FPS"), m_previewFpsComboBox);

    m_mainPathEdit = new QLineEdit(this);
    m_mainPathEdit->setClearButtonEnabled(true);
    m_mainPathEdit->setPlaceholderText(QStringLiteral("留空则沿用预览路径"));
    sharedForm->addRow(QStringLiteral("主码流路径"), m_mainPathEdit);

    m_mainFpsComboBox = new QComboBox(this);
    populateFpsOptions(m_mainFpsComboBox);
    sharedForm->addRow(QStringLiteral("主码流 FPS"), m_mainFpsComboBox);

    m_nvrPlaybackTemplateEdit = new QLineEdit(this);
    m_nvrPlaybackTemplateEdit->setClearButtonEnabled(true);
    m_nvrPlaybackTemplateEdit->setPlaceholderText(
        QStringLiteral("rtsp://{user}:{password}@{ip}:{port}/Streaming/tracks/{channel}?starttime={start}&endtime={end}"));
    sharedForm->addRow(QStringLiteral("NVR 回放模板"), m_nvrPlaybackTemplateEdit);

    layout->addLayout(sharedForm);

    auto *cameraTitle = new QLabel(QStringLiteral("12 路相机 IP"), this);
    layout->addWidget(cameraTitle);

    auto *cameraGrid = new QGridLayout();
    cameraGrid->setHorizontalSpacing(12);
    cameraGrid->setVerticalSpacing(8);
    m_ipEdits.reserve(m_cameraCount);
    for (int i = 0; i < m_cameraCount; ++i) {
        auto *label = new QLabel(QStringLiteral("CAM %1").arg(i + 1, 2, 10, QLatin1Char('0')), this);
        auto *edit = new QLineEdit(this);
        edit->setClearButtonEnabled(true);
        edit->setPlaceholderText(QStringLiteral("例如：192.168.2.%1").arg(200 + i + 1));
        cameraGrid->addWidget(label, i, 0);
        cameraGrid->addWidget(edit, i, 1);
        m_ipEdits.append(edit);
    }
    layout->addLayout(cameraGrid);

    auto *fieldTitle = new QLabel(QStringLiteral("场地与机位标定"), this);
    layout->addWidget(fieldTitle);

    auto *fieldTipLabel = new QLabel(QStringLiteral("每路相机覆盖滑冰场的一段距离；轨迹重建会按起止距离把 12 路画面拼接到同一条场地坐标上。"), this);
    fieldTipLabel->setWordWrap(true);
    layout->addWidget(fieldTipLabel);

    m_cameraFieldTable = new QTableWidget(m_cameraCount, 9, this);
    m_cameraFieldTable->setHorizontalHeaderLabels({
        QStringLiteral("参与轨迹"),
        QStringLiteral("用途"),
        QStringLiteral("起点m"),
        QStringLiteral("终点m"),
        QStringLiteral("横向m"),
        QStringLiteral("高度m"),
        QStringLiteral("朝向°"),
        QStringLiteral("俯仰°"),
        QStringLiteral("质量/兼容备注")
    });
    m_cameraFieldTable->verticalHeader()->setVisible(true);
    for (int i = 0; i < m_cameraCount; ++i) {
        m_cameraFieldTable->setVerticalHeaderItem(i, new QTableWidgetItem(QStringLiteral("CAM %1").arg(i + 1, 2, 10, QLatin1Char('0'))));
    }
    m_cameraFieldTable->horizontalHeader()->setStretchLastSection(true);
    m_cameraFieldTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
    m_cameraFieldTable->setMinimumHeight(250);
    m_cameraFieldTable->setAlternatingRowColors(true);
    m_cameraFieldTable->setSelectionMode(QAbstractItemView::SingleSelection);
    layout->addWidget(m_cameraFieldTable);

    auto *captureTitle = new QLabel(QStringLiteral("分析设置"), this);
    layout->addWidget(captureTitle);

    auto *captureForm = new QFormLayout();
    captureForm->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);
    captureForm->setFormAlignment(Qt::AlignTop);
    captureForm->setHorizontalSpacing(12);
    captureForm->setVerticalSpacing(10);

    m_precisionComboBox = new QComboBox(this);
    m_precisionComboBox->addItem(precisionLabel(QStringLiteral("fast")), QStringLiteral("fast"));
    m_precisionComboBox->addItem(precisionLabel(QStringLiteral("balanced")), QStringLiteral("balanced"));
    m_precisionComboBox->addItem(precisionLabel(QStringLiteral("high")), QStringLiteral("high"));
    captureForm->addRow(QStringLiteral("模型精度"), m_precisionComboBox);

    m_analysisSourceComboBox = new QComboBox(this);
    m_analysisSourceComboBox->addItem(QStringLiteral("预览流"), QStringLiteral("preview"));
    m_analysisSourceComboBox->addItem(QStringLiteral("主视图流"), QStringLiteral("main"));
    m_analysisSourceComboBox->setToolTip(QStringLiteral("多路 RTSP 分析默认使用预览流；主视图仍优先播放主码流。"));
    captureForm->addRow(QStringLiteral("分析流来源"), m_analysisSourceComboBox);

    m_analysisMaxStreamsComboBox = new QComboBox(this);
    populateStreamCountOptions(m_analysisMaxStreamsComboBox, m_cameraCount);
    captureForm->addRow(QStringLiteral("最大分析路数"), m_analysisMaxStreamsComboBox);

    m_fpsComboBox = new QComboBox(this);
    populateFpsOptions(m_fpsComboBox);
    m_fpsComboBox->setToolTip(QStringLiteral("多路 AI 分析按该目标 FPS 跳帧；实际 FPS 会受 GPU 和解码负载影响。"));
    captureForm->addRow(QStringLiteral("分析目标 FPS"), m_fpsComboBox);

    m_analysisAutoDegradeCheckBox = new QCheckBox(QStringLiteral("超载时自动降低非主机位分析频率"), this);
    m_analysisAutoDegradeCheckBox->setChecked(true);
    captureForm->addRow(QStringLiteral("自动降级"), m_analysisAutoDegradeCheckBox);

    layout->addLayout(captureForm);

    auto *storageTitle = new QLabel(QStringLiteral("视频存储"), this);
    layout->addWidget(storageTitle);

    auto *storageTipLabel = new QLabel(QStringLiteral("只管理已登记的视频资产；清理会删除本机文件并保留训练记录。"), this);
    storageTipLabel->setWordWrap(true);
    layout->addWidget(storageTipLabel);

    auto *storageForm = new QFormLayout();
    storageForm->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);
    storageForm->setFormAlignment(Qt::AlignTop);
    storageForm->setHorizontalSpacing(12);
    storageForm->setVerticalSpacing(10);

    auto *rootLayout = new QHBoxLayout();
    m_videoStorageRootEdit = new QLineEdit(this);
    m_videoStorageRootEdit->setClearButtonEnabled(true);
    m_videoStorageRootEdit->setPlaceholderText(QStringLiteral("默认：应用数据目录/recordings"));
    auto *browseStorageButton = new QPushButton(QStringLiteral("选择"), this);
    browseStorageButton->setProperty("role", "secondaryButton");
    rootLayout->addWidget(m_videoStorageRootEdit, 1);
    rootLayout->addWidget(browseStorageButton);
    storageForm->addRow(QStringLiteral("录像根目录"), rootLayout);

    m_videoStorageCapacitySpinBox = new QSpinBox(this);
    m_videoStorageCapacitySpinBox->setRange(1, 10240);
    m_videoStorageCapacitySpinBox->setSuffix(QStringLiteral(" GB"));
    storageForm->addRow(QStringLiteral("容量阈值"), m_videoStorageCapacitySpinBox);

    m_videoStorageRetentionSpinBox = new QSpinBox(this);
    m_videoStorageRetentionSpinBox->setRange(1, 3650);
    m_videoStorageRetentionSpinBox->setSuffix(QStringLiteral(" 天"));
    storageForm->addRow(QStringLiteral("保留天数"), m_videoStorageRetentionSpinBox);

    layout->addLayout(storageForm);

    auto *storageActionLayout = new QHBoxLayout();
    m_videoStorageStatusLabel = new QLabel(QStringLiteral("容量状态：未扫描"), this);
    m_videoStorageStatusLabel->setWordWrap(true);
    m_videoStorageStatusLabel->setProperty("role", "muted");
    m_videoStorageScanButton = new QPushButton(QStringLiteral("扫描容量"), this);
    m_videoStorageCleanupButton = new QPushButton(QStringLiteral("清理候选"), this);
    m_videoStorageScanButton->setProperty("role", "secondaryButton");
    m_videoStorageCleanupButton->setProperty("role", "secondaryButton");
    storageActionLayout->addWidget(m_videoStorageStatusLabel, 1);
    storageActionLayout->addWidget(m_videoStorageScanButton);
    storageActionLayout->addWidget(m_videoStorageCleanupButton);
    layout->addLayout(storageActionLayout);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    buttons->button(QDialogButtonBox::Ok)->setText(QStringLiteral("保存"));
    buttons->button(QDialogButtonBox::Cancel)->setText(QStringLiteral("取消"));
    layout->addWidget(buttons);

    connect(buttons, &QDialogButtonBox::accepted, this, [this]() {
        if (validateAndAccept()) {
            accept();
        }
    });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(importTemplateButton, &QPushButton::clicked, this, [this]() {
        importCameraTemplate();
    });
    connect(exportTemplateButton, &QPushButton::clicked, this, [this]() {
        exportCameraTemplate();
    });
    connect(m_connectivityTestButton, &QPushButton::clicked, this, [this]() {
        testCameraConnectivity();
    });
    connect(browseStorageButton, &QPushButton::clicked, this, [this]() {
        if (onBrowseVideoStorageRoot) {
            onBrowseVideoStorageRoot();
        }
    });
    connect(m_videoStorageScanButton, &QPushButton::clicked, this, [this]() {
        if (onScanVideoStorage) {
            onScanVideoStorage();
        }
    });
    connect(m_videoStorageCleanupButton, &QPushButton::clicked, this, [this]() {
        if (onShowVideoCleanupCandidates) {
            onShowVideoCleanupCandidates();
        }
    });
    connect(m_mainFpsComboBox,
            &QComboBox::currentIndexChanged,
            this,
            [this](int) {
                if (!m_mainFpsComboBox || !m_fpsComboBox) {
                    return;
                }

                const int mainFps = m_mainFpsComboBox->currentData().toInt();
                const int recordFpsIndex = m_fpsComboBox->findData(mainFps);
                m_fpsComboBox->setCurrentIndex(recordFpsIndex >= 0 ? recordFpsIndex
                                                                   : m_fpsComboBox->findData(120));
            });

    setStyleSheet(styleSheet() + QStringLiteral(R"QSS(
QDialog#framelessDialog QComboBox {
    min-height: 30px;
    border: 1px solid #2b3447;
    background: #101623;
    color: #e7edf7;
    padding: 4px 24px 4px 8px;
}
QDialog#framelessDialog QComboBox:hover,
QDialog#framelessDialog QComboBox:focus {
    border-color: #3b8dff;
    background: #111d31;
}
)QSS"));
}

void SystemSettingsDialog::setSharedCameraSettings(const SharedCameraSettings &settings)
{
    if (m_usernameEdit) {
        m_usernameEdit->setText(settings.username.trimmed());
    }
    if (m_passwordEdit) {
        m_passwordEdit->setText(settings.password);
    }
    if (m_portEdit) {
        m_portEdit->setText(settings.port.trimmed().isEmpty() ? QStringLiteral("554") : settings.port.trimmed());
    }
    if (m_previewPathEdit) {
        m_previewPathEdit->setText(normalizeStoredPath(settings.previewPath));
    }
    if (m_previewFpsComboBox) {
        const int previewFpsIndex = m_previewFpsComboBox->findData(settings.previewFps);
        m_previewFpsComboBox->setCurrentIndex(previewFpsIndex >= 0 ? previewFpsIndex : m_previewFpsComboBox->findData(30));
    }
    if (m_mainPathEdit) {
        m_mainPathEdit->setText(normalizeStoredPath(settings.mainPath));
    }
    if (m_mainFpsComboBox) {
        const int mainFpsIndex = m_mainFpsComboBox->findData(settings.mainFps);
        m_mainFpsComboBox->setCurrentIndex(mainFpsIndex >= 0 ? mainFpsIndex : m_mainFpsComboBox->findData(120));
    }
    if (m_nvrPlaybackTemplateEdit) {
        m_nvrPlaybackTemplateEdit->setText(settings.nvrPlaybackTemplate.trimmed());
    }
    if (m_fpsComboBox && m_mainFpsComboBox) {
        const int recordFps = m_mainFpsComboBox->currentData().toInt();
        const int recordFpsIndex = m_fpsComboBox->findData(recordFps);
        m_fpsComboBox->setCurrentIndex(recordFpsIndex >= 0 ? recordFpsIndex : m_fpsComboBox->findData(120));
    }
}

SharedCameraSettings SystemSettingsDialog::sharedCameraSettings() const
{
    SharedCameraSettings settings;
    settings.username = m_usernameEdit ? m_usernameEdit->text().trimmed() : QString();
    settings.password = m_passwordEdit ? m_passwordEdit->text() : QString();
    settings.port = m_portEdit ? m_portEdit->text().trimmed() : QStringLiteral("554");
    if (settings.port.isEmpty()) {
        settings.port = QStringLiteral("554");
    }
    settings.previewPath = normalizeStoredPath(m_previewPathEdit ? m_previewPathEdit->text() : QString());
    settings.previewFps = m_previewFpsComboBox ? m_previewFpsComboBox->currentData().toInt() : 30;
    if (settings.previewFps <= 0) {
        settings.previewFps = 30;
    }
    settings.mainPath = normalizeStoredPath(m_mainPathEdit ? m_mainPathEdit->text() : QString());
    settings.mainFps = m_mainFpsComboBox ? m_mainFpsComboBox->currentData().toInt() : 120;
    if (settings.mainFps <= 0) {
        settings.mainFps = 120;
    }
    settings.nvrPlaybackTemplate = m_nvrPlaybackTemplateEdit
                                       ? m_nvrPlaybackTemplateEdit->text().trimmed()
                                       : QString();
    return settings;
}

void SystemSettingsDialog::setCameraSlotSettings(const QVector<CameraSlotSettings> &settings)
{
    for (int i = 0; i < m_ipEdits.size(); ++i) {
        const CameraSlotSettings slot = i < settings.size() ? settings.at(i) : defaultCameraSlotSettings(i);
        m_ipEdits.at(i)->setText(slot.ip.trimmed());
        if (!m_cameraFieldTable) {
            continue;
        }

        m_cameraFieldTable->setItem(i, 0, makeEnabledItem(slot.trajectoryEnabled));
        m_cameraFieldTable->setItem(i, 1, makeTableItem(slot.role.trimmed().isEmpty() ? QStringLiteral("轨迹分段") : slot.role.trimmed()));
        m_cameraFieldTable->setItem(i, 2, makeTableItem(QString::number(slot.fieldStartM, 'f', 1)));
        m_cameraFieldTable->setItem(i, 3, makeTableItem(QString::number(slot.fieldEndM, 'f', 1)));
        m_cameraFieldTable->setItem(i, 4, makeTableItem(QString::number(slot.lateralOffsetM, 'f', 1)));
        m_cameraFieldTable->setItem(i, 5, makeTableItem(QString::number(slot.mountHeightM, 'f', 1)));
        m_cameraFieldTable->setItem(i, 6, makeTableItem(QString::number(slot.yawDeg, 'f', 1)));
        m_cameraFieldTable->setItem(i, 7, makeTableItem(QString::number(slot.pitchDeg, 'f', 1)));
        QStringList notes;
        if (!slot.qualityNote.trimmed().isEmpty()) {
            notes.append(slot.qualityNote.trimmed());
        }
        if (!slot.compatibilityNote.trimmed().isEmpty() && slot.compatibilityNote.trimmed() != slot.qualityNote.trimmed()) {
            notes.append(slot.compatibilityNote.trimmed());
        }
        const QString note = notes.join(QStringLiteral("；"));
        m_cameraFieldTable->setItem(i, 8, makeTableItem(note));
    }
}

QVector<CameraSlotSettings> SystemSettingsDialog::cameraSlotSettings() const
{
    QVector<CameraSlotSettings> settings;
    settings.reserve(m_ipEdits.size());
    for (int i = 0; i < m_ipEdits.size(); ++i) {
        CameraSlotSettings slot = defaultCameraSlotSettings(i);
        if (QLineEdit *edit = m_ipEdits.at(i)) {
            slot.ip = edit->text().trimmed();
        }
        if (m_cameraFieldTable) {
            const QTableWidgetItem *enabledItem = m_cameraFieldTable->item(i, 0);
            slot.trajectoryEnabled = !enabledItem || enabledItem->checkState() == Qt::Checked;
            slot.role = tableTextValue(m_cameraFieldTable, i, 1);
            if (slot.role.trimmed().isEmpty()) {
                slot.role = QStringLiteral("轨迹分段");
            }
            slot.fieldStartM = tableDoubleValue(m_cameraFieldTable, i, 2, slot.fieldStartM);
            slot.fieldEndM = tableDoubleValue(m_cameraFieldTable, i, 3, slot.fieldEndM);
            slot.lateralOffsetM = tableDoubleValue(m_cameraFieldTable, i, 4, slot.lateralOffsetM);
            slot.mountHeightM = tableDoubleValue(m_cameraFieldTable, i, 5, slot.mountHeightM);
            slot.yawDeg = tableDoubleValue(m_cameraFieldTable, i, 6, slot.yawDeg);
            slot.pitchDeg = tableDoubleValue(m_cameraFieldTable, i, 7, slot.pitchDeg);
            slot.qualityNote = tableTextValue(m_cameraFieldTable, i, 8);
            slot.compatibilityNote = slot.qualityNote;
        }
        settings.append(slot);
    }
    return settings;
}

void SystemSettingsDialog::setCapturePreferenceSettings(const CapturePreferenceSettings &settings)
{
    if (m_precisionComboBox) {
        const int precisionIndex = m_precisionComboBox->findData(settings.modelPrecision.trimmed());
        m_precisionComboBox->setCurrentIndex(precisionIndex >= 0 ? precisionIndex : 1);
    }
    if (m_analysisSourceComboBox) {
        const int sourceIndex = m_analysisSourceComboBox->findData(settings.analysisSource.trimmed());
        m_analysisSourceComboBox->setCurrentIndex(sourceIndex >= 0 ? sourceIndex : 0);
    }
    if (m_analysisMaxStreamsComboBox) {
        const int maxStreams = std::clamp(settings.analysisMaxStreams, 1, std::max(1, m_cameraCount));
        const int maxStreamsIndex = m_analysisMaxStreamsComboBox->findData(maxStreams);
        m_analysisMaxStreamsComboBox->setCurrentIndex(maxStreamsIndex >= 0
                                                          ? maxStreamsIndex
                                                          : m_analysisMaxStreamsComboBox->findData(std::max(1, m_cameraCount)));
    }
    if (m_fpsComboBox) {
        const int effectiveFps = settings.analysisTargetFps > 0 ? settings.analysisTargetFps : settings.fps;
        const int fpsIndex = m_fpsComboBox->findData(effectiveFps);
        m_fpsComboBox->setCurrentIndex(fpsIndex >= 0 ? fpsIndex : m_fpsComboBox->findData(5));
    }
    if (m_analysisAutoDegradeCheckBox) {
        m_analysisAutoDegradeCheckBox->setChecked(settings.analysisAutoDegrade);
    }
}

CapturePreferenceSettings SystemSettingsDialog::capturePreferenceSettings() const
{
    CapturePreferenceSettings settings;
    if (m_precisionComboBox) {
        settings.modelPrecision = m_precisionComboBox->currentData().toString().trimmed();
    }
    if (settings.modelPrecision.isEmpty()) {
        settings.modelPrecision = QStringLiteral("balanced");
    }

    if (m_analysisSourceComboBox) {
        settings.analysisSource = m_analysisSourceComboBox->currentData().toString().trimmed();
    }
    if (settings.analysisSource != QStringLiteral("main")) {
        settings.analysisSource = QStringLiteral("preview");
    }

    if (m_fpsComboBox) {
        settings.analysisTargetFps = m_fpsComboBox->currentData().toInt();
    }
    if (settings.analysisTargetFps <= 0) {
        settings.analysisTargetFps = 5;
    }
    settings.fps = settings.analysisTargetFps;

    if (m_analysisMaxStreamsComboBox) {
        settings.analysisMaxStreams = m_analysisMaxStreamsComboBox->currentData().toInt();
    }
    settings.analysisMaxStreams = std::clamp(settings.analysisMaxStreams, 1, std::max(1, m_cameraCount));
    if (m_analysisAutoDegradeCheckBox) {
        settings.analysisAutoDegrade = m_analysisAutoDegradeCheckBox->isChecked();
    }
    if (settings.fps <= 0) {
        settings.fps = 5;
    }
    return settings;
}

void SystemSettingsDialog::setVideoStorageSettings(const VideoStorageSettings &settings)
{
    if (m_videoStorageRootEdit) {
        m_videoStorageRootEdit->setText(settings.rootDir.trimmed());
    }
    if (m_videoStorageCapacitySpinBox) {
        m_videoStorageCapacitySpinBox->setValue(settings.capacityLimitGb > 0 ? settings.capacityLimitGb : 50);
    }
    if (m_videoStorageRetentionSpinBox) {
        m_videoStorageRetentionSpinBox->setValue(settings.retentionDays > 0 ? settings.retentionDays : 60);
    }
}

VideoStorageSettings SystemSettingsDialog::videoStorageSettings() const
{
    VideoStorageSettings settings;
    settings.rootDir = m_videoStorageRootEdit ? m_videoStorageRootEdit->text().trimmed() : QString();
    settings.capacityLimitGb = m_videoStorageCapacitySpinBox ? m_videoStorageCapacitySpinBox->value() : 50;
    settings.retentionDays = m_videoStorageRetentionSpinBox ? m_videoStorageRetentionSpinBox->value() : 60;
    if (settings.capacityLimitGb <= 0) {
        settings.capacityLimitGb = 50;
    }
    if (settings.retentionDays <= 0) {
        settings.retentionDays = 60;
    }
    return settings;
}

void SystemSettingsDialog::setVideoStorageStatus(const QString &status)
{
    if (m_videoStorageStatusLabel) {
        m_videoStorageStatusLabel->setText(status.trimmed().isEmpty()
                                               ? QStringLiteral("容量状态：未扫描")
                                               : status.trimmed());
    }
}

void SystemSettingsDialog::setVideoStorageActionsEnabled(bool enabled)
{
    if (m_videoStorageScanButton) {
        m_videoStorageScanButton->setEnabled(enabled);
    }
    if (m_videoStorageCleanupButton) {
        m_videoStorageCleanupButton->setEnabled(enabled);
    }
}

bool SystemSettingsDialog::validateAndAccept()
{
    bool hasConfiguredCamera = false;
    for (auto *edit : m_ipEdits) {
        if (edit && !edit->text().trimmed().isEmpty()) {
            hasConfiguredCamera = true;
            break;
        }
    }

    if (m_portEdit && m_portEdit->text().trimmed().isEmpty()) {
        m_portEdit->setText(QStringLiteral("554"));
    }

    bool ok = false;
    const int port = m_portEdit ? m_portEdit->text().trimmed().toInt(&ok) : 554;
    if (!ok || port <= 0 || port > 65535) {
        QMessageBox::warning(this,
                             QStringLiteral("端口无效"),
                             QStringLiteral("请输入 1 到 65535 之间的端口号。"));
        if (m_portEdit) {
            m_portEdit->setFocus();
        }
        return false;
    }

    if (hasConfiguredCamera && m_previewPathEdit && m_previewPathEdit->text().trimmed().isEmpty()) {
        QMessageBox::warning(this,
                             QStringLiteral("预览路径不能为空"),
                             QStringLiteral("至少配置一路相机时，预览路径必须填写。"));
        m_previewPathEdit->setFocus();
        return false;
    }

    if (m_cameraFieldTable) {
        for (int i = 0; i < m_cameraFieldTable->rowCount(); ++i) {
            const QTableWidgetItem *enabledItem = m_cameraFieldTable->item(i, 0);
            const bool enabled = !enabledItem || enabledItem->checkState() == Qt::Checked;
            if (!enabled) {
                continue;
            }

            const double startM = tableDoubleValue(m_cameraFieldTable, i, 2, 0.0);
            const double endM = tableDoubleValue(m_cameraFieldTable, i, 3, 0.0);
            if (endM <= startM) {
                QMessageBox::warning(this,
                                     QStringLiteral("场地段无效"),
                                     QStringLiteral("CAM %1 的终点距离必须大于起点距离。")
                                         .arg(i + 1, 2, 10, QLatin1Char('0')));
                m_cameraFieldTable->setCurrentCell(i, 3);
                return false;
            }
        }
    }

    if (m_previewPathEdit) {
        m_previewPathEdit->setText(normalizeStoredPath(m_previewPathEdit->text()));
    }
    if (m_mainPathEdit) {
        m_mainPathEdit->setText(normalizeStoredPath(m_mainPathEdit->text()));
    }
    if (m_nvrPlaybackTemplateEdit) {
        const QString templ = m_nvrPlaybackTemplateEdit->text().trimmed();
        if (!templ.isEmpty()
            && (!templ.contains(QStringLiteral("{ip}"))
                || !templ.contains(QStringLiteral("{start}"))
                || !templ.contains(QStringLiteral("{end}")))) {
            QMessageBox::warning(this,
                                 QStringLiteral("NVR 模板无效"),
                                 QStringLiteral("NVR 回放模板至少需要包含 {ip}、{start} 和 {end} 占位符。"));
            m_nvrPlaybackTemplateEdit->setFocus();
            return false;
        }
        m_nvrPlaybackTemplateEdit->setText(templ);
    }
    if (m_videoStorageCapacitySpinBox && m_videoStorageCapacitySpinBox->value() <= 0) {
        QMessageBox::warning(this,
                             QStringLiteral("容量阈值无效"),
                             QStringLiteral("视频存储容量阈值必须大于 0。"));
        m_videoStorageCapacitySpinBox->setFocus();
        return false;
    }
    if (m_videoStorageRetentionSpinBox && m_videoStorageRetentionSpinBox->value() <= 0) {
        QMessageBox::warning(this,
                             QStringLiteral("保留天数无效"),
                             QStringLiteral("视频保留天数必须大于 0。"));
        m_videoStorageRetentionSpinBox->setFocus();
        return false;
    }
    if (m_videoStorageRootEdit) {
        m_videoStorageRootEdit->setText(m_videoStorageRootEdit->text().trimmed());
    }
    return true;
}

void SystemSettingsDialog::importCameraTemplate()
{
    const QString filePath = QFileDialog::getOpenFileName(this,
                                                          QStringLiteral("导入摄像头配置模板"),
                                                          QString(),
                                                          cameraConfigTemplateFileFilter());
    if (filePath.trimmed().isEmpty()) {
        return;
    }

    const CameraConfigTemplateResult result = loadCameraConfigTemplate(filePath, m_cameraCount);
    if (!result.ok) {
        QMessageBox::warning(this,
                             QStringLiteral("导入失败"),
                             result.error);
        return;
    }

    const int answer = QMessageBox::question(this,
                                             QStringLiteral("确认导入模板"),
                                             QStringLiteral("将用模板内容覆盖当前系统设置表单，点击“保存”后才会写入本机配置。\n\n%1")
                                                 .arg(cameraConfigTemplateSummary(result.data)),
                                             QMessageBox::Yes | QMessageBox::No,
                                             QMessageBox::No);
    if (answer != QMessageBox::Yes) {
        return;
    }

    setSharedCameraSettings(result.data.shared);
    setCameraSlotSettings(result.data.cameras);
    setCapturePreferenceSettings(result.data.capture);
}

void SystemSettingsDialog::exportCameraTemplate()
{
    const QString filePath = QFileDialog::getSaveFileName(this,
                                                          QStringLiteral("导出摄像头配置模板"),
                                                          QStringLiteral("camera-config-template.json"),
                                                          cameraConfigTemplateFileFilter());
    if (filePath.trimmed().isEmpty()) {
        return;
    }

    QString errorMessage;
    if (!saveCameraConfigTemplate(filePath,
                                  sharedCameraSettings(),
                                  capturePreferenceSettings(),
                                  cameraSlotSettings(),
                                  &errorMessage)) {
        QMessageBox::warning(this,
                             QStringLiteral("导出失败"),
                             errorMessage);
        return;
    }

    QMessageBox::information(this,
                             QStringLiteral("导出完成"),
                             QStringLiteral("摄像头配置模板已导出。"));
}

void SystemSettingsDialog::testCameraConnectivity()
{
    SharedCameraSettings shared = sharedCameraSettings();
    QVector<CameraSlotSettings> cameras = cameraSlotSettings();

    if (shared.previewPath.trimmed().isEmpty()) {
        QMessageBox::warning(this,
                             QStringLiteral("预览路径不能为空"),
                             QStringLiteral("请先填写预览路径，再执行连通测试。"));
        if (m_previewPathEdit) {
            m_previewPathEdit->setFocus();
        }
        return;
    }

    auto *dialog = new QDialog(this);
    dialog->setWindowTitle(QStringLiteral("批量连通测试"));
    dialog->setMinimumSize(820, 460);
    dialog->setWindowFlag(Qt::WindowCloseButtonHint, false);

    auto *dialogLayout = new QVBoxLayout(dialog);
    auto *summaryLabel = new QLabel(QStringLiteral("正在测试 0/%1 路相机...").arg(cameras.size()), dialog);
    summaryLabel->setWordWrap(true);
    dialogLayout->addWidget(summaryLabel);

    auto *resultTable = new QTableWidget(cameras.size(), 7, dialog);
    resultTable->setHorizontalHeaderLabels({
        QStringLiteral("机位"),
        QStringLiteral("IP"),
        QStringLiteral("状态"),
        QStringLiteral("协议"),
        QStringLiteral("分辨率"),
        QStringLiteral("帧率"),
        QStringLiteral("结果")
    });
    resultTable->horizontalHeader()->setStretchLastSection(true);
    resultTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
    resultTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    resultTable->setSelectionMode(QAbstractItemView::SingleSelection);
    resultTable->setAlternatingRowColors(true);
    for (int i = 0; i < cameras.size(); ++i) {
        resultTable->setItem(i, 0, makeTableItem(QStringLiteral("CAM %1").arg(i + 1, 2, 10, QLatin1Char('0'))));
        resultTable->setItem(i, 1, makeTableItem(cameras.at(i).ip.trimmed()));
        resultTable->setItem(i, 2, makeTableItem(QStringLiteral("等待")));
        resultTable->setItem(i, 3, makeTableItem(QStringLiteral("-")));
        resultTable->setItem(i, 4, makeTableItem(QStringLiteral("-")));
        resultTable->setItem(i, 5, makeTableItem(QStringLiteral("-")));
        resultTable->setItem(i, 6, makeTableItem(QStringLiteral("-")));
    }
    dialogLayout->addWidget(resultTable);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, dialog);
    buttons->button(QDialogButtonBox::Close)->setText(QStringLiteral("关闭"));
    buttons->button(QDialogButtonBox::Close)->setEnabled(false);
    dialogLayout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::rejected, dialog, &QDialog::reject);

    auto *thread = new QThread(dialog);
    auto *tester = new CameraConnectivityTester(shared, cameras);
    tester->moveToThread(thread);

    if (m_connectivityTestButton) {
        m_connectivityTestButton->setEnabled(false);
    }

    auto *successCount = new int(0);
    auto *failedCount = new int(0);
    auto *skippedCount = new int(0);
    auto *threadRunning = new bool(true);

    connect(thread, &QThread::started, tester, &CameraConnectivityTester::run);
    connect(tester,
            &CameraConnectivityTester::progress,
            dialog,
            [=](int completed, int total, const CameraConnectivityResult &result) {
                if (result.cameraIndex < 0 || result.cameraIndex >= resultTable->rowCount()) {
                    return;
                }
                if (result.success) {
                    ++(*successCount);
                } else if (result.skipped) {
                    ++(*skippedCount);
                } else {
                    ++(*failedCount);
                }

                resultTable->setItem(result.cameraIndex, 1, makeTableItem(result.ip));
                resultTable->setItem(result.cameraIndex, 2, makeTableItem(result.status));
                resultTable->setItem(result.cameraIndex, 3, makeTableItem(result.transport));
                resultTable->setItem(result.cameraIndex, 4, makeTableItem(result.resolution));
                resultTable->setItem(result.cameraIndex, 5, makeTableItem(result.frameRate));
                resultTable->setItem(result.cameraIndex, 6, makeTableItem(result.message));
                summaryLabel->setText(QStringLiteral("正在测试 %1/%2 路相机；成功 %3，失败 %4，跳过 %5。")
                                          .arg(completed)
                                          .arg(total)
                                          .arg(*successCount)
                                          .arg(*failedCount)
                                          .arg(*skippedCount));
            });
    connect(tester,
            &CameraConnectivityTester::finished,
            dialog,
            [=](const QVector<CameraConnectivityResult> &) {
                summaryLabel->setText(QStringLiteral("测试完成：成功 %1，失败 %2，跳过 %3。结果仅本次显示，不会自动写入配置。")
                                          .arg(*successCount)
                                          .arg(*failedCount)
                                          .arg(*skippedCount));
                buttons->button(QDialogButtonBox::Close)->setEnabled(true);
                dialog->setWindowFlag(Qt::WindowCloseButtonHint, true);
                dialog->show();
                if (m_connectivityTestButton) {
                    m_connectivityTestButton->setEnabled(true);
                }
                thread->quit();
            });
    connect(tester, &CameraConnectivityTester::finished, tester, &QObject::deleteLater);
    connect(thread, &QThread::finished, dialog, [=]() {
        *threadRunning = false;
        thread->deleteLater();
    });
    connect(dialog, &QDialog::finished, dialog, [=]() {
        if (m_connectivityTestButton) {
            m_connectivityTestButton->setEnabled(true);
        }
        if (*threadRunning) {
            thread->quit();
            thread->wait(5000);
        }
        delete successCount;
        delete failedCount;
        delete skippedCount;
        delete threadRunning;
    });

    thread->start();
    dialog->exec();
}
