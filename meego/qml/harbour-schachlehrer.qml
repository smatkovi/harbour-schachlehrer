/*
    Copyright (C) 2026 smatkovi

    This file is part of harbour-schachlehrer.

    harbour-schachlehrer is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    harbour-schachlehrer is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with harbour-schachlehrer. If not, see <https://www.gnu.org/licenses/>.

    SPDX-License-Identifier: GPL-3.0-or-later
*/
import QtQuick 1.1
import com.nokia.meego 1.0

// src/main.cpp installs the one TeacherEngine instance as the root context
// property `teacher` (docs/design.md §4); every page reads it from there
// rather than passing it down the page stack.
PageStackWindow {
    id: app

    // Silica's palette is a dark one; the MeeGo components start light.
    Component.onCompleted: theme.inverted = true

    initialPage: Component { MainPage { } }
}
