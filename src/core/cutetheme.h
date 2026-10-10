/*
 * Strawberry Music Player
 * Copyright 2026, Strawberry contributors
 *
 * Strawberry is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * Strawberry is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Strawberry.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#ifndef CUTETHEME_H
#define CUTETHEME_H

#include "config.h"

#include <QList>
#include <QPair>
#include <QString>
#include <QColor>
#include <QImage>

// User customizable pastel theme: colors are stored in the settings and turned into the application palette and stylesheet.
namespace CuteTheme {

constexpr char kSettingsGroup[] = "CuteTheme";

struct Colors {
  QColor accent;       // Main accent (buttons, selection, sliders)
  QColor accent2;      // Secondary accent (gradients, sidebar)
  QColor window;       // Window background
  QColor base;         // List and input background
  QColor text;         // Text
  int radius = 10;     // Corner roundness in pixels
  int mascot_opacity = 100;  // Opacity of the playlist mascot in percent
  bool modern_playlist = true;  // Modern playlist look: covers, two line titles, rounded rows, now playing card
  bool modern_icons = true;     // Monochrome outline icons tinted with the text color, instead of the colorful pastel ones

  bool operator==(const Colors &other) const;
  bool operator!=(const Colors &other) const { return !(*this == other); }

  // Derived colors
  QColor AccentLight() const;   // Hover backgrounds
  QColor AccentSoft() const;    // Borders
  QColor AccentStrong() const;  // Emphasized text
  QColor TextMuted() const;
  QColor Header() const;        // Menu bar, headers, status bar
  QColor OnAccent() const;      // Text on top of the accent color
  QColor SidebarTop() const;
  QColor SidebarBottom() const;
  bool IsDark() const;
};

QList<QPair<QString, Colors>> Presets();
Colors Default();

Colors Load();
void Save(const Colors &colors);

// Applies the palette and stylesheet to the application and remembers the colors as current.
void Apply(const Colors &colors);
const Colors &Current();

QColor Mix(const QColor &a, const QColor &b, const qreal amount_b);

// File dialog filter for background images (PNG, JPEG, WebP, ...).
QString ImageFileFilter();

// Loads a background image of any supported format (PNG, JPEG, WebP, ...), detecting the format from the file contents
// so files with a wrong extension work too, and applying the EXIF orientation. Returns a null image and sets error on failure.
QImage LoadImageFile(const QString &filename, QString *error = nullptr);

}  // namespace CuteTheme

#endif  // CUTETHEME_H
