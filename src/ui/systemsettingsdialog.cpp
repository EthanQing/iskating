#include "systemsettingsdialog.h"

#include "cameraconfigtemplate.h"
#include "cameraconnectivitytester.h"
#include "animatedbutton.h"

#include <QComboBox>
#include <QAbstractItemView>
#include <QApplication>
#include <QCheckBox>
#include <QButtonGroup>
#include <QDialogButtonBox>
#include <QDialog>
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
#include <QScrollArea>
#include <QScreen>
#include <QStackedWidget>
#include <QSpinBox>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QThread>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
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
    setMinimumSize(900, 640);
    QSize initialSize(1100, 760);
    if (const QScreen *screen = QApplication::primaryScreen()) {
        const QSize availableSize = screen->availableGeometry().size() - QSize(24, 24);
        initialSize.setWidth(std::max(900, std::min(initialSize.width(), availableSize.width())));
        initialSize.setHeight(std::max(640, std::min(initialSize.height(), availableSize.height())));
    }
    resize(initialSize);
    setSizeGripEnabled(true);
    if (auto *titleBar = findChild<QWidget *>(QStringLiteral("dialogTitleBar"))) {
        titleBar->setFixedHeight(48);
    }

    auto *dialogLayout = contentLayout();
    dialogLayout->setContentsMargins(18, 16, 18, 16);
    dialogLayout->setSpacing(0);

    auto *settingsBody = new QWidget(this);
    settingsBody->setObjectName(QStringLiteral("settingsBody"));
    auto *bodyLayout = new QHBoxLayout(settingsBody);
    bodyLayout->setContentsMargins(0, 0, 0, 0);
    bodyLayout->setSpacing(20);

    auto *navigation = new QWidget(settingsBody);
    navigation->setObjectName(QStringLiteral("settingsNavigation"));
    navigation->setFixedWidth(180);
    auto *navigationLayout = new QVBoxLayout(navigation);
    navigationLayout->setContentsMargins(0, 8, 0, 8);
    navigationLayout->setSpacing(6);

    auto *navigationTitle = new QLabel(QStringLiteral("系统设置"), navigation);
    navigationTitle->setObjectName(QStringLiteral("navigationTitle"));
    navigationLayout->addWidget(navigationTitle);
    navigationLayout->addSpacing(18);

    auto *pages = new QStackedWidget(settingsBody);
    pages->setObjectName(QStringLiteral("settingsPages"));

    auto createPage = [this, pages](const QString &title, const QString &description) {
        auto *scrollArea = new QScrollArea(pages);
        scrollArea->setObjectName(QStringLiteral("settingsPage"));
        scrollArea->setWidgetResizable(true);
        scrollArea->setFrameShape(QFrame::NoFrame);
        scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

        auto *page = new QWidget(scrollArea);
        auto *pageLayout = new QVBoxLayout(page);
        pageLayout->setContentsMargins(24, 20, 24, 24);
        pageLayout->setSpacing(14);

        auto *titleLabel = new QLabel(title, page);
        titleLabel->setObjectName(QStringLiteral("pageTitle"));
        pageLayout->addWidget(titleLabel);

        auto *descriptionLabel = new QLabel(description, page);
        descriptionLabel->setObjectName(QStringLiteral("pageDescription"));
        descriptionLabel->setWordWrap(true);
        pageLayout->addWidget(descriptionLabel);
        pageLayout->addSpacing(8);

        scrollArea->setWidget(page);
        pages->addWidget(scrollArea);
        return pageLayout;
    };

    auto *videoPageLayout = createPage(
        QStringLiteral("视频与摄像头"),
        QStringLiteral("配置公共 RTSP 参数和固定机位。每一路摄像头只需填写 IP 地址。"));

    auto *toolbar = new QHBoxLayout();
    toolbar->setSpacing(8);
    toolbar->addStretch();
    auto *importTemplateButton = new AnimatedButton(this);
    importTemplateButton->setText(QStringLiteral("导入模板"));
    auto *exportTemplateButton = new AnimatedButton(this);
    exportTemplateButton->setText(QStringLiteral("导出模板"));
    m_connectivityTestButton = new AnimatedButton(this);
    m_connectivityTestButton->setText(QStringLiteral("连通测试"));
    const QVector<QPushButton *> templateButtons{
        importTemplateButton,
        exportTemplateButton,
        m_connectivityTestButton
    };
    for (auto *button : templateButtons) {
        button->setProperty("variant", "ghost");
        button->setMinimumHeight(36);
    }
    importTemplateButton->setToolTip(QStringLiteral("从 JSON 文件导入公共 RTSP 参数和 12 路相机配置。"));
    exportTemplateButton->setToolTip(QStringLiteral("把当前公共 RTSP 参数和 12 路相机配置导出为 JSON 模板。"));
    m_connectivityTestButton->setToolTip(QStringLiteral("按当前表单配置逐路测试 RTSP 预览流，结果不会自动保存。"));
    toolbar->addWidget(importTemplateButton);
    toolbar->addWidget(exportTemplateButton);
    toolbar->addWidget(m_connectivityTestButton);
    videoPageLayout->addLayout(toolbar);

    auto *rtspTitle = new QLabel(QStringLiteral("公共 RTSP"), this);
    rtspTitle->setObjectName(QStringLiteral("sectionTitle"));
    videoPageLayout->addWidget(rtspTitle);

    auto configureForm = [](QFormLayout *form) {
        form->setLabelAlignment(Qt::AlignLeft | Qt::AlignVCenter);
        form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
        form->setHorizontalSpacing(16);
        form->setVerticalSpacing(12);
    };

    auto *sharedForms = new QHBoxLayout();
    sharedForms->setSpacing(28);
    auto *accountForm = new QFormLayout();
    auto *streamForm = new QFormLayout();
    configureForm(accountForm);
    configureForm(streamForm);

    m_usernameEdit = new QLineEdit(this);
    m_usernameEdit->setClearButtonEnabled(true);
    m_usernameEdit->setPlaceholderText(QStringLiteral("例如：admin"));
    accountForm->addRow(QStringLiteral("账号"), m_usernameEdit);

    m_passwordEdit = new QLineEdit(this);
    m_passwordEdit->setEchoMode(QLineEdit::Password);
    m_passwordEdit->setClearButtonEnabled(true);
    m_passwordEdit->setPlaceholderText(QStringLiteral("可留空"));
    accountForm->addRow(QStringLiteral("密码"), m_passwordEdit);

    m_portEdit = new QLineEdit(this);
    m_portEdit->setClearButtonEnabled(true);
    m_portEdit->setPlaceholderText(QStringLiteral("554"));
    m_portEdit->setValidator(new QIntValidator(1, 65535, m_portEdit));
    accountForm->addRow(QStringLiteral("端口"), m_portEdit);

    m_previewPathEdit = new QLineEdit(this);
    m_previewPathEdit->setClearButtonEnabled(true);
    m_previewPathEdit->setPlaceholderText(QStringLiteral("Streaming/Channels/102"));
    streamForm->addRow(QStringLiteral("预览路径"), m_previewPathEdit);

    m_previewFpsComboBox = new QComboBox(this);
    populateFpsOptions(m_previewFpsComboBox);
    streamForm->addRow(QStringLiteral("预览 FPS"), m_previewFpsComboBox);

    m_mainPathEdit = new QLineEdit(this);
    m_mainPathEdit->setClearButtonEnabled(true);
    m_mainPathEdit->setPlaceholderText(QStringLiteral("留空则沿用预览路径"));
    streamForm->addRow(QStringLiteral("主码流路径"), m_mainPathEdit);

    m_mainFpsComboBox = new QComboBox(this);
    populateFpsOptions(m_mainFpsComboBox);
    streamForm->addRow(QStringLiteral("主码流 FPS"), m_mainFpsComboBox);

    sharedForms->addLayout(accountForm, 1);
    sharedForms->addLayout(streamForm, 1);
    videoPageLayout->addLayout(sharedForms);

    auto *playbackForm = new QFormLayout();
    configureForm(playbackForm);
    m_nvrPlaybackTemplateEdit = new QLineEdit(this);
    m_nvrPlaybackTemplateEdit->setClearButtonEnabled(true);
    m_nvrPlaybackTemplateEdit->setPlaceholderText(
        QStringLiteral("rtsp://{user}:{password}@{ip}:{port}/Streaming/tracks/{channel}?starttime={start}&endtime={end}"));
    playbackForm->addRow(QStringLiteral("NVR 回放模板"), m_nvrPlaybackTemplateEdit);
    videoPageLayout->addLayout(playbackForm);
    videoPageLayout->addSpacing(8);

    auto *cameraTitle = new QLabel(QStringLiteral("12 路 Camera"), this);
    cameraTitle->setObjectName(QStringLiteral("sectionTitle"));
    videoPageLayout->addWidget(cameraTitle);

    auto *cameraGrid = new QGridLayout();
    cameraGrid->setHorizontalSpacing(18);
    cameraGrid->setVerticalSpacing(10);
    m_ipEdits.reserve(m_cameraCount);
    for (int i = 0; i < m_cameraCount; ++i) {
        const int group = i / 4;
        const int row = i % 4;
        const int column = group * 2;
        auto *label = new QLabel(
            QStringLiteral("CAM %1").arg(i + 1, 2, 10, QLatin1Char('0')), this);
        label->setObjectName(QStringLiteral("cameraLabel"));
        auto *edit = new QLineEdit(this);
        edit->setClearButtonEnabled(true);
        edit->setPlaceholderText(QStringLiteral("—"));
        cameraGrid->addWidget(label, row, column);
        cameraGrid->addWidget(edit, row, column + 1);
        cameraGrid->setColumnStretch(column + 1, 1);
        m_ipEdits.append(edit);
    }
    videoPageLayout->addLayout(cameraGrid);
    videoPageLayout->addStretch();

    auto *fieldPageLayout = createPage(
        QStringLiteral("场地与轨迹"),
        QStringLiteral("设置各机位覆盖的场地区间，并维护用于二维轨迹拼接的四点标定。"));

    auto *fieldTitle = new QLabel(QStringLiteral("场地与机位标定"), this);
    fieldTitle->setObjectName(QStringLiteral("sectionTitle"));
    fieldPageLayout->addWidget(fieldTitle);

    auto *fieldTipLabel = new QLabel(
        QStringLiteral("双击“四点标定”单元格，可填写画面像素点及其对应的统一场地坐标。"), this);
    fieldTipLabel->setObjectName(QStringLiteral("pageDescription"));
    fieldTipLabel->setWordWrap(true);
    fieldPageLayout->addWidget(fieldTipLabel);

    m_cameraFieldTable = new QTableWidget(m_cameraCount, 10, this);
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
    m_cameraFieldTable->setHorizontalHeaderItem(9, new QTableWidgetItem(QStringLiteral("四点标定")));
    for (int i = 0; i < m_cameraCount; ++i) {
        m_cameraFieldTable->setVerticalHeaderItem(
            i, new QTableWidgetItem(QStringLiteral("CAM %1").arg(i + 1, 2, 10, QLatin1Char('0'))));
    }
    m_cameraFieldTable->horizontalHeader()->setStretchLastSection(true);
    m_cameraFieldTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
    m_cameraFieldTable->setMinimumHeight(450);
    m_cameraFieldTable->setAlternatingRowColors(true);
    m_cameraFieldTable->setSelectionMode(QAbstractItemView::SingleSelection);
    connect(m_cameraFieldTable, &QTableWidget::cellDoubleClicked, this, [this](int row, int column) {
        if (column != 9 || row < 0 || row >= m_cameraCalibrationJson.size()) {
            return;
        }

        QDialog dialog(this);
        dialog.setWindowTitle(QStringLiteral("CAM %1 四点冰面标定").arg(row + 1, 2, 10, QLatin1Char('0')));
        dialog.setMinimumSize(680, 420);
        auto *dialogLayout = new QVBoxLayout(&dialog);
        auto *table = new QTableWidget(4, 4, &dialog);
        table->setHorizontalHeaderLabels({
            QStringLiteral("像素X"),
            QStringLiteral("像素Y"),
            QStringLiteral("场地X(m)"),
            QStringLiteral("场地Y(m)")
        });
        const QJsonArray saved =
            QJsonDocument::fromJson(m_cameraCalibrationJson.at(row).toUtf8()).array();
        const QStringList keys{
            QStringLiteral("px"),
            QStringLiteral("py"),
            QStringLiteral("x"),
            QStringLiteral("y")
        };
        for (int point = 0; point < 4; ++point) {
            const QJsonObject coordinate =
                point < saved.size() ? saved.at(point).toObject() : QJsonObject{};
            for (int field = 0; field < keys.size(); ++field) {
                table->setItem(point,
                               field,
                               new QTableWidgetItem(
                                   QString::number(coordinate.value(keys.at(field)).toDouble(), 'f', 3)));
            }
        }
        dialogLayout->addWidget(new QLabel(
            QStringLiteral("填写画面像素点和对应的统一场地米制坐标；四点不得共线。"), &dialog));
        dialogLayout->addWidget(table);

        auto *buttons =
            new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, &dialog);
        buttons->button(QDialogButtonBox::Save)->setText(QStringLiteral("保存"));
        buttons->button(QDialogButtonBox::Cancel)->setText(QStringLiteral("取消"));
        dialogLayout->addWidget(buttons);
        connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
        connect(buttons, &QDialogButtonBox::accepted, &dialog, [&]() {
            QJsonArray points;
            for (int point = 0; point < 4; ++point) {
                QJsonObject coordinate;
                for (int field = 0; field < keys.size(); ++field) {
                    bool ok = false;
                    const double number =
                        table->item(point, field)->text().trimmed().toDouble(&ok);
                    if (!ok) {
                        QMessageBox::warning(
                            &dialog,
                            QStringLiteral("标定无效"),
                            QStringLiteral("四个点的所有坐标必须为数字。"));
                        return;
                    }
                    coordinate.insert(keys.at(field), number);
                }
                points.append(coordinate);
            }
            auto hasArea = [&](const QString &first, const QString &second) {
                double maximum = 0.0;
                for (int a = 0; a < 4; ++a) {
                    for (int b = a + 1; b < 4; ++b) {
                        for (int c = b + 1; c < 4; ++c) {
                            const QJsonObject p1 = points.at(a).toObject();
                            const QJsonObject p2 = points.at(b).toObject();
                            const QJsonObject p3 = points.at(c).toObject();
                            maximum = std::max(
                                maximum,
                                std::abs(
                                    (p2.value(first).toDouble() - p1.value(first).toDouble())
                                        * (p3.value(second).toDouble() - p1.value(second).toDouble())
                                    - (p2.value(second).toDouble() - p1.value(second).toDouble())
                                        * (p3.value(first).toDouble() - p1.value(first).toDouble())));
                        }
                    }
                }
                return maximum > 1e-6;
            };
            if (!hasArea(QStringLiteral("px"), QStringLiteral("py"))
                || !hasArea(QStringLiteral("x"), QStringLiteral("y"))) {
                QMessageBox::warning(
                    &dialog,
                    QStringLiteral("标定无效"),
                    QStringLiteral("像素点和场地点都必须包含不共线的三点。"));
                return;
            }
            m_cameraCalibrationJson[row] =
                QString::fromUtf8(QJsonDocument(points).toJson(QJsonDocument::Compact));
            m_cameraFieldTable->setItem(row, 9, makeTableItem(QStringLiteral("已标定")));
            dialog.accept();
        });
        dialog.exec();
    });
    fieldPageLayout->addWidget(m_cameraFieldTable, 1);

    auto *analysisPageLayout = createPage(
        QStringLiteral("AI 分析"),
        QStringLiteral("控制实时分析使用的视频流、处理规模和性能偏好。"));

    auto addExplainedRow = [this, configureForm](
                               QVBoxLayout *pageLayout,
                               const QString &title,
                               const QString &description,
                               QWidget *field) {
        auto *form = new QFormLayout();
        configureForm(form);
        auto *label = new QLabel(title, this);
        label->setObjectName(QStringLiteral("settingLabel"));
        form->addRow(label, field);
        pageLayout->addLayout(form);
        auto *descriptionLabel = new QLabel(description, this);
        descriptionLabel->setObjectName(QStringLiteral("settingDescription"));
        descriptionLabel->setWordWrap(true);
        pageLayout->addWidget(descriptionLabel);
    };

    m_precisionComboBox = new QComboBox(this);
    m_precisionComboBox->addItem(precisionLabel(QStringLiteral("fast")), QStringLiteral("fast"));
    m_precisionComboBox->addItem(precisionLabel(QStringLiteral("balanced")), QStringLiteral("balanced"));
    m_precisionComboBox->addItem(precisionLabel(QStringLiteral("high")), QStringLiteral("high"));
    addExplainedRow(
        analysisPageLayout,
        QStringLiteral("模型精度"),
        QStringLiteral("在分析速度和识别精度之间选择适合当前设备的平衡。"),
        m_precisionComboBox);

    m_analysisSourceComboBox = new QComboBox(this);
    m_analysisSourceComboBox->addItem(QStringLiteral("预览流"), QStringLiteral("preview"));
    m_analysisSourceComboBox->addItem(QStringLiteral("主视图流"), QStringLiteral("main"));
    addExplainedRow(
        analysisPageLayout,
        QStringLiteral("分析流来源"),
        QStringLiteral("预览流适合多机位分析；主视图仍会优先播放主码流。"),
        m_analysisSourceComboBox);

    m_fpsComboBox = new QComboBox(this);
    populateFpsOptions(m_fpsComboBox);
    addExplainedRow(
        analysisPageLayout,
        QStringLiteral("分析目标 FPS"),
        QStringLiteral("实际处理帧率会根据设备性能和视频解码负载变化。"),
        m_fpsComboBox);

    m_analysisMaxStreamsComboBox = new QComboBox(this);
    populateStreamCountOptions(m_analysisMaxStreamsComboBox, m_cameraCount);
    addExplainedRow(
        analysisPageLayout,
        QStringLiteral("最大分析路数"),
        QStringLiteral("限制同时参与 AI 分析的摄像头数量。"),
        m_analysisMaxStreamsComboBox);

    m_analysisAutoDegradeCheckBox =
        new QCheckBox(QStringLiteral("超载时自动降低非主机位分析频率"), this);
    m_analysisAutoDegradeCheckBox->setChecked(true);
    addExplainedRow(
        analysisPageLayout,
        QStringLiteral("自动降级"),
        QStringLiteral("设备负载过高时优先保持主机位分析的连续性。"),
        m_analysisAutoDegradeCheckBox);
    analysisPageLayout->addStretch();

    auto *storagePageLayout = createPage(
        QStringLiteral("存储"),
        QStringLiteral("设置录像保存位置、容量限制、保留天数与清理候选。"));

    auto *storageTitle = new QLabel(QStringLiteral("视频存储"), this);
    storageTitle->setObjectName(QStringLiteral("sectionTitle"));
    storagePageLayout->addWidget(storageTitle);

    auto *storageForm = new QFormLayout();
    configureForm(storageForm);

    auto *rootLayout = new QHBoxLayout();
    rootLayout->setSpacing(8);
    m_videoStorageRootEdit = new QLineEdit(this);
    m_videoStorageRootEdit->setClearButtonEnabled(true);
    m_videoStorageRootEdit->setPlaceholderText(QStringLiteral("默认：应用数据目录/recordings"));
    auto *browseStorageButton = new AnimatedButton(this);
    browseStorageButton->setText(QStringLiteral("浏览"));
    browseStorageButton->setProperty("variant", "ghost");
    browseStorageButton->setMinimumHeight(36);
    rootLayout->addWidget(m_videoStorageRootEdit, 1);
    rootLayout->addWidget(browseStorageButton);
    storageForm->addRow(QStringLiteral("根目录"), rootLayout);

    m_videoStorageCapacitySpinBox = new QSpinBox(this);
    m_videoStorageCapacitySpinBox->setRange(1, 10240);
    m_videoStorageCapacitySpinBox->setSuffix(QStringLiteral(" GB"));
    storageForm->addRow(QStringLiteral("容量限制"), m_videoStorageCapacitySpinBox);

    m_videoStorageRetentionSpinBox = new QSpinBox(this);
    m_videoStorageRetentionSpinBox->setRange(1, 3650);
    m_videoStorageRetentionSpinBox->setSuffix(QStringLiteral(" 天"));
    storageForm->addRow(QStringLiteral("保留天数"), m_videoStorageRetentionSpinBox);
    storagePageLayout->addLayout(storageForm);

    auto *storageDescription = new QLabel(
        QStringLiteral("清理候选只包含已登记的视频资产；训练记录会继续保留。"), this);
    storageDescription->setObjectName(QStringLiteral("settingDescription"));
    storageDescription->setWordWrap(true);
    storagePageLayout->addWidget(storageDescription);
    storagePageLayout->addSpacing(12);

    m_videoStorageStatusLabel = new QLabel(QStringLiteral("容量状态：未扫描"), this);
    m_videoStorageStatusLabel->setObjectName(QStringLiteral("storageStatus"));
    m_videoStorageStatusLabel->setWordWrap(true);
    storagePageLayout->addWidget(m_videoStorageStatusLabel);

    auto *storageActionLayout = new QHBoxLayout();
    storageActionLayout->setSpacing(8);
    m_videoStorageScanButton = new AnimatedButton(this);
    m_videoStorageScanButton->setText(QStringLiteral("扫描"));
    m_videoStorageCleanupButton = new AnimatedButton(this);
    m_videoStorageCleanupButton->setText(QStringLiteral("清理候选"));
    const QVector<QPushButton *> storageButtons{
        m_videoStorageScanButton,
        m_videoStorageCleanupButton
    };
    for (auto *button : storageButtons) {
        button->setProperty("variant", "ghost");
        button->setMinimumHeight(36);
    }
    storageActionLayout->addWidget(m_videoStorageScanButton);
    storageActionLayout->addWidget(m_videoStorageCleanupButton);
    storageActionLayout->addStretch();
    storagePageLayout->addLayout(storageActionLayout);
    storagePageLayout->addStretch();

    auto *navigationGroup = new QButtonGroup(this);
    navigationGroup->setExclusive(true);
    const QStringList navigationLabels{
        QStringLiteral("视频与摄像头"),
        QStringLiteral("场地与轨迹"),
        QStringLiteral("AI 分析"),
        QStringLiteral("存储")
    };
    const QStringList navigationIcons{
        QStringLiteral(":/icons/video.svg"),
        QStringLiteral(":/icons/live.svg"),
        QStringLiteral(":/icons/suggestion.svg"),
        QStringLiteral(":/icons/save.svg")
    };
    for (int i = 0; i < navigationLabels.size(); ++i) {
        auto *button = new AnimatedButton(navigation);
        button->setText(navigationLabels.at(i));
        button->setIconSource(navigationIcons.at(i));
        button->setProperty("role", "nav");
        button->setCheckable(true);
        button->setMinimumHeight(42);
        navigationGroup->addButton(button, i);
        navigationLayout->addWidget(button);
        connect(button, &QPushButton::clicked, this, [pages, i]() {
            pages->setCurrentIndex(i);
        });
        connect(button, &QPushButton::toggled, this, [button](bool checked) {
            button->setProperty("active", checked);
            button->update();
        });
        if (i == 0) {
            button->setChecked(true);
            button->setProperty("active", true);
        }
    }
    navigationLayout->addStretch();

    bodyLayout->addWidget(navigation);
    bodyLayout->addWidget(pages, 1);
    dialogLayout->addWidget(settingsBody, 1);

    auto *actionBar = new QWidget(this);
    actionBar->setObjectName(QStringLiteral("settingsActionBar"));
    auto *actionLayout = new QHBoxLayout(actionBar);
    actionLayout->setContentsMargins(0, 14, 0, 0);
    actionLayout->setSpacing(10);
    auto *actionStatus = new QLabel(QStringLiteral("修改将在保存后生效"), actionBar);
    actionStatus->setObjectName(QStringLiteral("actionStatus"));
    auto *cancelButton = new AnimatedButton(actionBar);
    cancelButton->setText(QStringLiteral("取消"));
    cancelButton->setProperty("variant", "ghost");
    cancelButton->setMinimumSize(96, 40);
    auto *saveButton = new AnimatedButton(actionBar);
    saveButton->setText(QStringLiteral("保存设置"));
    saveButton->setProperty("variant", "primary");
    saveButton->setMinimumSize(112, 40);
    actionLayout->addWidget(actionStatus);
    actionLayout->addStretch();
    actionLayout->addWidget(cancelButton);
    actionLayout->addWidget(saveButton);
    dialogLayout->addWidget(actionBar);

    connect(saveButton, &QPushButton::clicked, this, [this]() {
        if (validateAndAccept()) {
            accept();
        }
    });
    connect(cancelButton, &QPushButton::clicked, this, &QDialog::reject);
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
    connect(m_mainFpsComboBox, &QComboBox::currentIndexChanged, this, [this](int) {
        if (!m_mainFpsComboBox || !m_fpsComboBox) {
            return;
        }
        const int mainFps = m_mainFpsComboBox->currentData().toInt();
        const int analysisFpsIndex = m_fpsComboBox->findData(mainFps);
        m_fpsComboBox->setCurrentIndex(
            analysisFpsIndex >= 0 ? analysisFpsIndex : m_fpsComboBox->findData(120));
    });

    setStyleSheet(QStringLiteral(R"QSS(
QDialog#framelessDialog {
    background: #0C1118;
    color: #e6edf7;
    font-size: 14px;
}
QDialog#framelessDialog #settingsNavigation {
    background: #0e1521;
    border-radius: 10px;
}
QDialog#framelessDialog #navigationTitle {
    color: #f3f7fd;
    font-size: 18px;
    font-weight: 600;
    padding: 4px 14px;
}
QDialog#framelessDialog #dialogTitleBar {
    background: #101720;
    border-bottom: 1px solid #202C3A;
}
QDialog#framelessDialog #dialogTitleLabel {
    color: #F4F7FA;
    font-size: 14px;
    font-weight: 600;
}
QDialog#framelessDialog #dialogCloseButton {
    min-width: 28px;
    min-height: 24px;
    max-width: 28px;
    max-height: 24px;
    border: none;
    background: transparent;
    color: #A2AFBF;
    padding: 0;
    font-size: 18px;
}
QDialog#framelessDialog #dialogCloseButton:hover {
    background: #542925;
    color: #FFFFFF;
}
QDialog#framelessDialog #settingsPages,
QDialog#framelessDialog QScrollArea#settingsPage,
QDialog#framelessDialog QScrollArea#settingsPage > QWidget > QWidget {
    border: none;
    background: #101720;
}
QDialog#framelessDialog #pageTitle {
    color: #f4f7fb;
    font-size: 22px;
    font-weight: 600;
}
QDialog#framelessDialog #pageDescription,
QDialog#framelessDialog #settingDescription,
QDialog#framelessDialog #actionStatus {
    color: #8290a5;
    font-size: 13px;
}
QDialog#framelessDialog #sectionTitle {
    color: #dce5f2;
    font-size: 16px;
    font-weight: 600;
    padding-top: 4px;
}
QDialog#framelessDialog #settingLabel,
QDialog#framelessDialog #cameraLabel {
    color: #bcc8d8;
}
QDialog#framelessDialog QLineEdit,
QDialog#framelessDialog QComboBox,
QDialog#framelessDialog QSpinBox {
    min-height: 36px;
    max-height: 36px;
    border: 1px solid #263247;
    border-radius: 6px;
    background: #151E29;
    color: #e6edf7;
    padding: 0 10px;
    selection-background-color: #377dcc;
}
QDialog#framelessDialog QComboBox {
    padding-right: 28px;
}
QDialog#framelessDialog QLineEdit:hover,
QDialog#framelessDialog QComboBox:hover,
QDialog#framelessDialog QSpinBox:hover {
    border-color: #3a4962;
    background: #152033;
}
QDialog#framelessDialog QLineEdit:focus,
QDialog#framelessDialog QComboBox:focus,
QDialog#framelessDialog QSpinBox:focus {
    border-color: #38BDF8;
    background: #152033;
}
QDialog#framelessDialog QCheckBox {
    min-height: 36px;
    color: #d4deeb;
    spacing: 8px;
}
QDialog#framelessDialog #storageStatus {
    min-height: 52px;
    border-radius: 8px;
    background: #151E29;
    color: #b8c7da;
    padding: 12px;
}
QDialog#framelessDialog QTableWidget {
    border: none;
    border-radius: 8px;
    background: #101824;
    alternate-background-color: #131e2d;
    gridline-color: #202d40;
    color: #dbe4f0;
}
QDialog#framelessDialog QHeaderView::section {
    min-height: 34px;
    border: none;
    border-right: 1px solid #253247;
    border-bottom: 1px solid #253247;
    background: #172234;
    color: #aebcd0;
    padding: 4px 7px;
}
QDialog#framelessDialog #settingsActionBar {
    border-top: 1px solid #202b3c;
    background: #0C1118;
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
    m_cameraCalibrationJson = QVector<QString>(m_ipEdits.size());
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
        m_cameraCalibrationJson[i] = slot.calibrationJson;
        m_cameraFieldTable->setItem(i, 9, makeTableItem(slot.calibrationJson.trimmed().isEmpty() ? QStringLiteral("未标定") : QStringLiteral("已标定")));
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
            slot.calibrationJson = i < m_cameraCalibrationJson.size() ? m_cameraCalibrationJson.at(i) : QString();
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
    dialog->setMinimumSize(1180, 460);
    dialog->setWindowFlag(Qt::WindowCloseButtonHint, false);

    auto *dialogLayout = new QVBoxLayout(dialog);
    auto *summaryLabel = new QLabel(QStringLiteral("正在测试 0/%1 路相机...").arg(cameras.size()), dialog);
    summaryLabel->setWordWrap(true);
    dialogLayout->addWidget(summaryLabel);

    auto *resultTable = new QTableWidget(cameras.size(), 12, dialog);
    resultTable->setHorizontalHeaderLabels({
        QStringLiteral("机位"),
        QStringLiteral("IP"),
        QStringLiteral("地址"),
        QStringLiteral("状态"),
        QStringLiteral("协议"),
        QStringLiteral("Open 耗时"),
        QStringLiteral("首帧耗时"),
        QStringLiteral("分辨率"),
        QStringLiteral("帧率"),
        QStringLiteral("失败阶段"),
        QStringLiteral("错误码"),
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
        for (int column = 2; column < resultTable->columnCount(); ++column) {
            resultTable->setItem(i, column, makeTableItem(column == 3 ? QStringLiteral("等待") : QStringLiteral("-")));
        }
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
                resultTable->setItem(result.cameraIndex, 2, makeTableItem(result.addressStatus));
                resultTable->setItem(result.cameraIndex, 3, makeTableItem(result.status));
                resultTable->setItem(result.cameraIndex, 4, makeTableItem(result.transport));
                resultTable->setItem(result.cameraIndex, 5, makeTableItem(result.openElapsedMs < 0 ? QStringLiteral("-") : QStringLiteral("%1 ms").arg(result.openElapsedMs)));
                resultTable->setItem(result.cameraIndex, 6, makeTableItem(result.firstFrameElapsedMs < 0 ? QStringLiteral("-") : QStringLiteral("%1 ms").arg(result.firstFrameElapsedMs)));
                resultTable->setItem(result.cameraIndex, 7, makeTableItem(result.resolution));
                resultTable->setItem(result.cameraIndex, 8, makeTableItem(result.frameRate));
                resultTable->setItem(result.cameraIndex, 9, makeTableItem(result.failureStage));
                const QString errorCode = result.ffmpegErrorCode.isEmpty()
                                              ? result.errorCode
                                              : QStringLiteral("%1 (%2)").arg(result.errorCode, result.ffmpegErrorCode);
                resultTable->setItem(result.cameraIndex, 10, makeTableItem(errorCode));
                resultTable->setItem(result.cameraIndex, 11, makeTableItem(result.message));
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
