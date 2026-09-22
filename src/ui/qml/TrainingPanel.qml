pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
Rectangle {
    id: root
    property var presentation: windowPresentation
    Theme { id: theme }
    color: theme.surface
    ScrollView {
        id: scroll
        anchors.fill: parent
        anchors.margins: 16
        contentWidth: availableWidth
        clip: true
        ColumnLayout {
            width: scroll.availableWidth
            spacing: 14
            Label { text: "训练工作台"; color: theme.text; font.pixelSize: 20; font.bold: true }
            Label { text: "当前运动员"; color: theme.muted }
            ComboBox {
                id: athletes
                objectName: "athleteSelector"
                Layout.fillWidth: true
                model: root.presentation.athletes
                textRole: "name"
                valueRole: "id"
                currentIndex: {
                    const list = root.presentation.athletes
                    const selectedId = root.presentation.selectedAthleteId
                    for (let i = 0; i < list.length; ++i) if (list[i].id === selectedId) return i
                    return -1
                }
                onActivated: root.presentation.selectAthlete(currentValue)
                Accessible.name: "当前运动员"
                palette.buttonText: theme.text
                palette.text: theme.text
                palette.window: theme.elevated
                palette.base: theme.elevated
                palette.button: theme.elevated
                // Keep the popup inside this QQuickWidget, away from native video.
                popup.y: athletes.height
                popup.height: Math.min(180, Math.max(50, scroll.height - athletes.y - athletes.height - 8))
            }
            Label { Layout.fillWidth: true; text: root.presentation.identity; color: theme.stateColor(root.presentation.identityState); wrapMode: Text.Wrap }
            Label { Layout.fillWidth: true; text: root.presentation.trainingState; color: theme.stateColor(root.presentation.trainingRole); wrapMode: Text.Wrap }
            Rectangle { Layout.fillWidth: true; implicitHeight: 1; color: theme.border }
            Label { text: "实时状态"; color: theme.text; font.bold: true }
            Repeater {
                model: [{label: "当前机位", value: root.presentation.source}, {label: "检测目标", value: root.presentation.detectionCount}, {label: "训练时长", value: root.presentation.duration}, {label: "当前速度", value: root.presentation.speed}]
                RowLayout {
                    id: metric
                    required property var modelData
                    Layout.fillWidth: true
                    Label { text: metric.modelData.label; color: theme.muted }
                    Label { Layout.fillWidth: true; text: metric.modelData.value; color: theme.text; horizontalAlignment: Text.AlignRight; wrapMode: Text.Wrap }
                }
            }
            Rectangle { Layout.fillWidth: true; implicitHeight: 1; color: theme.border }
            Label { text: "AI 状态"; color: theme.text; font.bold: true }
            Label { Layout.fillWidth: true; text: "目标检测 · " + root.presentation.modelText; color: theme.stateColor(root.presentation.modelState); wrapMode: Text.Wrap }
            Label { Layout.fillWidth: true; text: "身份识别 · " + root.presentation.identityAvailability; color: theme.stateColor(root.presentation.identityAvailabilityState); wrapMode: Text.Wrap }
            Label { Layout.fillWidth: true; text: root.presentation.modelTip; color: theme.muted; font.pixelSize: 11; wrapMode: Text.Wrap; visible: text.length > 0 }
            RowLayout {
                Layout.fillWidth: true
                ActionButton { objectName: "trainingSettings"; Layout.fillWidth: true; text: "训练设置"; selected: root.presentation.settingsExpanded; onClicked: root.presentation.requestTrainingSettings() }
                ActionButton { objectName: "manualIdentity"; Layout.fillWidth: true; text: "身份绑定"; onClicked: root.presentation.requestManualIdentity() }
            }
            ActionButton { objectName: "startTraining"; Layout.fillWidth: true; text: root.presentation.startText; primary: true; enabled: root.presentation.canStart; onClicked: root.presentation.requestStart() }
            RowLayout {
                Layout.fillWidth: true
                ActionButton { objectName: "pauseTraining"; Layout.fillWidth: true; text: "暂停"; enabled: root.presentation.canPause; onClicked: root.presentation.requestPause() }
                ActionButton { objectName: "stopTraining"; Layout.fillWidth: true; text: "停止"; enabled: root.presentation.canStop; onClicked: root.presentation.requestStop() }
            }
            ActionButton { objectName: "saveTraining"; Layout.fillWidth: true; text: "保存训练"; enabled: root.presentation.canSave; onClicked: root.presentation.requestSave() }
            Label { objectName: "saveTip"; Layout.fillWidth: true; text: root.presentation.saveTip; visible: text.length > 0; color: theme.warning; wrapMode: Text.Wrap; textFormat: Text.PlainText }
        }
    }
}
