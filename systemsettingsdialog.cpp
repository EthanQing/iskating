#include "systemsettingsdialog.h"

#include <QComboBox>
#include <QAbstractItemView>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGridLayout>
#include <QHeaderView>
#include <QIntValidator>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>

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

    for (int fps : {15, 25, 30, 50, 60, 90, 120}) {
        comboBox->addItem(QStringLiteral("%1 FPS").arg(fps), fps);
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

    m_fpsComboBox = new QComboBox(this);
    populateFpsOptions(m_fpsComboBox);
    m_fpsComboBox->setEnabled(false);
    m_fpsComboBox->setToolTip(QStringLiteral("分析记录 FPS 跟随主码流 FPS 自动同步。"));
    captureForm->addRow(QStringLiteral("分析记录 FPS（跟随主码流）"), m_fpsComboBox);

    layout->addLayout(captureForm);

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
    if (m_fpsComboBox) {
        const int effectiveFps = m_mainFpsComboBox
                                     ? m_mainFpsComboBox->currentData().toInt()
                                     : settings.fps;
        const int fpsIndex = m_fpsComboBox->findData(effectiveFps);
        m_fpsComboBox->setCurrentIndex(fpsIndex >= 0 ? fpsIndex : m_fpsComboBox->findData(120));
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

    if (m_mainFpsComboBox) {
        settings.fps = m_mainFpsComboBox->currentData().toInt();
    } else if (m_fpsComboBox) {
        settings.fps = m_fpsComboBox->currentData().toInt();
    }
    if (settings.fps <= 0) {
        settings.fps = 120;
    }
    return settings;
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
    return true;
}
