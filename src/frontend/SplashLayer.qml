// Pegasus Frontend
// Copyright (C) 2017-2018  Mátyás Mustoha
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program. If not, see <http://www.gnu.org/licenses/>.


import QtQuick 2.0


Rectangle {
    id: root
    color: "#0B1220"
    anchors.fill: parent

    property real progress: 0
    property bool showDataProgressText: true
    property alias stage: gameCounter.text

    Behavior on progress { NumberAnimation {} }


    Item {
        id: brand
        width: Math.min(parent.width, parent.height) * 0.55
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.top: parent.top
        anchors.bottom: parent.verticalCenter
        anchors.bottomMargin: vpx(-20)

        Image {
            id: mark
            source: "assets/logo-mark.png"
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.top: parent.top
            anchors.bottom: title.top
            anchors.bottomMargin: vpx(18)
            anchors.topMargin: vpx(24)
            fillMode: Image.PreserveAspectFit
            verticalAlignment: Image.AlignBottom
        }

        Text {
            id: title
            text: "Chiron"
            color: "#F4F1EA"
            font.pixelSize: vpx(48)
            font.family: global.fonts.sans
            font.bold: true
            horizontalAlignment: Text.AlignHCenter

            anchors.horizontalCenter: parent.horizontalCenter
            anchors.bottom: parent.bottom
        }
    }

    Rectangle {
        id: progressRoot

        width: Math.min(brand.width, parent.width * 0.55)
        height: vpx(8)
        radius: height * 0.5
        color: "#243044"

        anchors.top: brand.bottom
        anchors.topMargin: vpx(28)
        anchors.horizontalCenter: parent.horizontalCenter
        clip: true

        Rectangle {
            width: parent.width * Math.max(0.02, Math.min(1.0, root.progress))
            height: parent.height
            radius: parent.radius
            color: "#C4A767"
            anchors.left: parent.left
            anchors.verticalCenter: parent.verticalCenter
        }
    }

    Text {
        id: gameCounter
        visible: showDataProgressText

        color: "#9AA3B2"
        font.pixelSize: vpx(16)
        font.family: global.fonts.sans

        anchors.top: progressRoot.bottom
        anchors.topMargin: vpx(14)
        anchors.horizontalCenter: progressRoot.horizontalCenter
    }
}
