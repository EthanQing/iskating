pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
Rectangle {
    id: root
    property var presentation: windowPresentation
    Theme { id: theme }
    color: theme.background
    ScrollView {
        id: scroll
        anchors.fill: parent
        anchors.margins: 10
        contentWidth: availableWidth
        clip: true
        ColumnLayout {
            width: scroll.availableWidth
            spacing: 10
            Label { Layout.fillWidth: true; Layout.minimumWidth: 0; elide: Text.ElideRight; text: root.presentation.sidebarExpanded ? "iSkating" : "iS"; color: theme.text; font.pixelSize: 24; font.bold: true; Layout.topMargin: 18; Layout.bottomMargin: 20 }
            Repeater {
                model: [{title: "实时训练", page: 0, icon: "live.svg"}, {title: "历史复盘", page: 1, icon: "history.svg"}, {title: "系统状态", page: 3, icon: "system-status.svg"}]
                ActionButton {
                    id: navigation
                    required property var modelData
                    objectName: "navigation" + modelData.page
                    Layout.fillWidth: true; Layout.minimumWidth: 0
                    implicitWidth: 44
                    implicitHeight: 48
                    text: root.presentation.sidebarExpanded ? modelData.title : ""
                    hint: modelData.title
                    selected: root.presentation.activePage === modelData.page
                    onClicked: root.presentation.navigate(modelData.page)
                    icon.source: root.presentation.sidebarExpanded ? "" : "qrc:/icons/" + modelData.icon
                }
            }
            Rectangle { Layout.fillWidth: true; Layout.minimumWidth: 0; Layout.topMargin: 18; Layout.bottomMargin: 8; implicitHeight: 1; color: theme.border }
            Repeater {
                model: [{title: "人员管理", icon: "person.svg", action: "requestPersonManagement"}, {title: "比赛管理", icon: "competition.svg", action: "requestCompetitionManagement"}, {title: "系统设置", icon: "settings.svg", action: "requestSystemSettings"}]
                ActionButton {
                    id: utility
                    required property var modelData
                    objectName: modelData.action
                    Layout.fillWidth: true; Layout.minimumWidth: 0
                    implicitWidth: 44
                    text: root.presentation.sidebarExpanded ? modelData.title : ""
                    hint: modelData.title
                    onClicked: root.presentation[modelData.action]()
                    icon.source: root.presentation.sidebarExpanded ? "" : "qrc:/icons/" + modelData.icon
                }
            }
            ActionButton {
                objectName: "toggleSidebar"
                Layout.fillWidth: true; Layout.minimumWidth: 0
                implicitWidth: 44
                text: root.presentation.sidebarExpanded ? "收起导航 ‹" : "›"
                hint: root.presentation.sidebarExpanded ? "收起导航" : "展开导航"
                onClicked: root.presentation.requestToggleSidebar()
            }
        }
    }
}
