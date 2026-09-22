pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
Rectangle {
    id: root
    property var presentation: windowPresentation
    Theme { id: theme }
    color: theme.surface
    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 12
        spacing: 8
        RowLayout {
            Layout.fillWidth: true
            Label { objectName: "pageTitle"; text: root.presentation.pageTitle; color: theme.text; font.pixelSize: 21; font.bold: true }
            Label { Layout.fillWidth: true; text: root.presentation.activePage === 0 ? root.presentation.source : ""; color: theme.muted; elide: Text.ElideRight }
            Label { text: root.presentation.serviceText; color: theme.stateColor(root.presentation.serviceState); font.pixelSize: 12 }
        }
        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            ActionButton { objectName: "importVideo"; text: "导入视频"; onClicked: root.presentation.requestImportVideo() }
            ActionButton { objectName: "offlineAnalysis"; text: "完整分析"; onClicked: root.presentation.requestOfflineAnalysis() }
            ActionButton { objectName: "taskCenter"; text: "任务中心"; onClicked: root.presentation.requestTaskCenter() }
            ActionButton { objectName: "environmentCheck"; visible: root.presentation.activePage === 3; text: root.presentation.checkText; enabled: root.presentation.canCheck; primary: true; onClicked: root.presentation.requestCheck() }
            Label { Layout.fillWidth: true; text: root.presentation.activePage === 3 ? root.presentation.checkResult : ""; elide: Text.ElideRight; color: theme.muted }
            ActionButton { objectName: "toggleFullScreen"; implicitWidth: 76; text: root.presentation.fullScreen ? "退出全屏" : "全屏 F11"; onClicked: root.presentation.requestToggleFullScreen() }
        }
    }
}
