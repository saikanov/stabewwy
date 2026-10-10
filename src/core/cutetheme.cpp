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

#include "config.h"

#include <algorithm>

#include <QApplication>
#include <QGuiApplication>
#include <QStyleHints>
#include <QPalette>
#include <QFile>
#include <QIODevice>
#include <QImageReader>
#include <QStringList>

#include "core/logging.h"
#include "core/settings.h"
#include "cutetheme.h"

using namespace Qt::Literals::StringLiterals;

namespace CuteTheme {

namespace {

Colors MakeColors(const char *accent, const char *accent2, const char *window, const char *base, const char *text) {
  Colors c;
  c.accent = QColor(QLatin1String(accent));
  c.accent2 = QColor(QLatin1String(accent2));
  c.window = QColor(QLatin1String(window));
  c.base = QColor(QLatin1String(base));
  c.text = QColor(QLatin1String(text));
  return c;
}

Colors &CurrentColors() {
  static Colors current = Default();
  return current;
}

}  // namespace

bool Colors::operator==(const Colors &other) const {
  return accent == other.accent && accent2 == other.accent2 && window == other.window && base == other.base && text == other.text && radius == other.radius && mascot_opacity == other.mascot_opacity && modern_playlist == other.modern_playlist && modern_icons == other.modern_icons;
}

QColor Mix(const QColor &a, const QColor &b, const qreal amount_b) {
  const qreal t = std::clamp(amount_b, 0.0, 1.0);
  return QColor::fromRgbF(static_cast<float>(a.redF() * (1.0 - t) + b.redF() * t),
                          static_cast<float>(a.greenF() * (1.0 - t) + b.greenF() * t),
                          static_cast<float>(a.blueF() * (1.0 - t) + b.blueF() * t));
}

QString ImageFileFilter() {

  return QObject::tr("All images (*.png *.jpg *.jpeg *.jfif *.webp *.bmp *.gif *.tif *.tiff)") + u";;"_s +
         QObject::tr("PNG images (*.png)") + u";;"_s +
         QObject::tr("JPEG images (*.jpg *.jpeg *.jfif)") + u";;"_s +
         QObject::tr("WebP images (*.webp)") + u";;"_s +
         QObject::tr("All files (*)");

}

QImage LoadImageFile(const QString &filename, QString *error) {

  if (filename.isEmpty()) {
    if (error) *error = QObject::tr("No image selected");
    return QImage();
  }

  QImageReader reader(filename);
  reader.setDecideFormatFromContent(true);
  reader.setAutoTransform(true);
  QImage image = reader.read();
  if (image.isNull()) {
    qLog(Error) << "Could not load background image" << filename << reader.errorString();
    if (error) *error = reader.errorString();
  }
  else if (error) {
    error->clear();
  }

  return image;

}

bool Colors::IsDark() const { return base.lightnessF() < 0.5; }
QColor Colors::AccentLight() const { return Mix(accent, base, 0.78); }
QColor Colors::AccentSoft() const { return Mix(accent, base, 0.5); }
QColor Colors::AccentStrong() const { return IsDark() ? accent.lighter(115) : accent.darker(125); }
QColor Colors::TextMuted() const { return Mix(text, base, 0.4); }
QColor Colors::Header() const { return Mix(accent, window, 0.82); }
QColor Colors::OnAccent() const { return accent.lightnessF() > 0.62 ? QColor(0x14, 0x14, 0x18) : QColor(Qt::white); }
QColor Colors::SidebarTop() const { return Mix(accent, window, 0.78); }
QColor Colors::SidebarBottom() const { return Mix(accent2, window, 0.72); }

QList<QPair<QString, Colors>> Presets() {

  return QList<QPair<QString, Colors>>()
    << qMakePair(QObject::tr("Sakura Pink"), MakeColors("#FF8FB8", "#C9A9FF", "#FFF0F6", "#FFFAFC", "#5B3A5E"))
    << qMakePair(QObject::tr("Lavender Dream"), MakeColors("#B48CFF", "#FF9EC7", "#F5F0FF", "#FCFAFF", "#4B3A6B"))
    << qMakePair(QObject::tr("Mint Soda"), MakeColors("#4CC9A0", "#8FD3FF", "#EEFBF6", "#FAFFFD", "#2F5149"))
    << qMakePair(QObject::tr("Peach Cream"), MakeColors("#FF9F7A", "#FFC86B", "#FFF4EC", "#FFFCF8", "#6B4436"))
    << qMakePair(QObject::tr("Sky Blue"), MakeColors("#6FB6FF", "#B9A6FF", "#EFF6FF", "#FAFCFF", "#34466B"))
    << qMakePair(QObject::tr("Midnight Kawaii"), MakeColors("#FF8FB8", "#9D7CFF", "#221B30", "#2B233D", "#F6E8F2"))
    // Dark themes
    << qMakePair(QObject::tr("Graphite"), MakeColors("#8AB4F8", "#A78BFA", "#1B1D21", "#22252A", "#E6E8EB"))
    << qMakePair(QObject::tr("OLED Black"), MakeColors("#4FD1C5", "#63B3ED", "#000000", "#0B0B0C", "#EDEDED"))
    << qMakePair(QObject::tr("Nord"), MakeColors("#88C0D0", "#81A1C1", "#2E3440", "#3B4252", "#ECEFF4"))
    << qMakePair(QObject::tr("Dracula"), MakeColors("#BD93F9", "#FF79C6", "#21222C", "#282A36", "#F8F8F2"))
    << qMakePair(QObject::tr("Tokyo Night"), MakeColors("#7AA2F7", "#BB9AF7", "#16161E", "#1A1B26", "#C0CAF5"))
    << qMakePair(QObject::tr("Catppuccin Mocha"), MakeColors("#CBA6F7", "#F5C2E7", "#181825", "#1E1E2E", "#CDD6F4"))
    << qMakePair(QObject::tr("Gruvbox Dark"), MakeColors("#FABD2F", "#FE8019", "#1D2021", "#282828", "#EBDBB2"))
    << qMakePair(QObject::tr("Forest"), MakeColors("#6FCF97", "#56CCF2", "#121A16", "#18221D", "#E3EFE7"))
    << qMakePair(QObject::tr("Crimson Night"), MakeColors("#F05454", "#F97316", "#151116", "#1D171F", "#F1E9EC"))
    << qMakePair(QObject::tr("Ocean Deep"), MakeColors("#38BDF8", "#22D3EE", "#0B1220", "#0F1A2B", "#E2E8F0"))
    << qMakePair(QObject::tr("Sunset Dark"), MakeColors("#F59E0B", "#EC4899", "#18131B", "#211A24", "#F5EDE6"))
    << qMakePair(QObject::tr("Monochrome"), MakeColors("#D4D4D8", "#A1A1AA", "#18181B", "#1F1F23", "#F4F4F5"));

}

Colors Default() {
  return Presets().constFirst().second;
}

Colors Load() {

  const Colors def = Default();
  Settings s;
  s.beginGroup(QLatin1String(kSettingsGroup));
  Colors c;
  c.accent = QColor(s.value("accent", def.accent.name()).toString());
  c.accent2 = QColor(s.value("accent2", def.accent2.name()).toString());
  c.window = QColor(s.value("window", def.window.name()).toString());
  c.base = QColor(s.value("base", def.base.name()).toString());
  c.text = QColor(s.value("text", def.text.name()).toString());
  c.radius = s.value("radius", def.radius).toInt();
  c.mascot_opacity = s.value("mascot_opacity", def.mascot_opacity).toInt();
  c.modern_playlist = s.value("modern_playlist", def.modern_playlist).toBool();
  c.modern_icons = s.value("modern_icons", def.modern_icons).toBool();
  s.endGroup();

  if (!c.accent.isValid()) c.accent = def.accent;
  if (!c.accent2.isValid()) c.accent2 = def.accent2;
  if (!c.window.isValid()) c.window = def.window;
  if (!c.base.isValid()) c.base = def.base;
  if (!c.text.isValid()) c.text = def.text;

  return c;

}

void Save(const Colors &c) {

  Settings s;
  s.beginGroup(QLatin1String(kSettingsGroup));
  s.setValue("accent", c.accent.name());
  s.setValue("accent2", c.accent2.name());
  s.setValue("window", c.window.name());
  s.setValue("base", c.base.name());
  s.setValue("text", c.text.name());
  s.setValue("radius", c.radius);
  s.setValue("mascot_opacity", c.mascot_opacity);
  s.setValue("modern_playlist", c.modern_playlist);
  s.setValue("modern_icons", c.modern_icons);
  s.endGroup();

}

const Colors &Current() {
  return CurrentColors();
}

void Apply(const Colors &c) {

  CurrentColors() = c;

  QApplication::setStyle(u"Fusion"_s);
#if QT_VERSION >= QT_VERSION_CHECK(6, 8, 0)
  QGuiApplication::styleHints()->setColorScheme(c.IsDark() ? Qt::ColorScheme::Dark : Qt::ColorScheme::Light);
#endif

  const QColor disabled_text = Mix(c.text, c.base, 0.6);

  QPalette palette;
  palette.setColor(QPalette::Window, c.window);
  palette.setColor(QPalette::WindowText, c.text);
  palette.setColor(QPalette::Base, c.base);
  palette.setColor(QPalette::AlternateBase, Mix(c.accent, c.base, 0.88));
  palette.setColor(QPalette::Text, c.text);
  palette.setColor(QPalette::PlaceholderText, c.TextMuted());
  palette.setColor(QPalette::Button, c.AccentLight());
  palette.setColor(QPalette::ButtonText, c.text);
  palette.setColor(QPalette::BrightText, c.AccentStrong());
  palette.setColor(QPalette::Light, c.IsDark() ? c.window.lighter(140) : QColor(Qt::white));
  palette.setColor(QPalette::Midlight, c.AccentLight());
  palette.setColor(QPalette::Mid, c.AccentSoft());
  palette.setColor(QPalette::Dark, Mix(c.accent, c.text, 0.35));
  palette.setColor(QPalette::Shadow, Mix(c.text, c.window, 0.3));
  palette.setColor(QPalette::Highlight, c.accent);
  palette.setColor(QPalette::HighlightedText, c.OnAccent());
  palette.setColor(QPalette::ToolTipBase, c.base);
  palette.setColor(QPalette::ToolTipText, c.text);
  palette.setColor(QPalette::Link, c.accent2);
  palette.setColor(QPalette::LinkVisited, c.accent2.darker(115));
  palette.setColor(QPalette::Disabled, QPalette::Text, disabled_text);
  palette.setColor(QPalette::Disabled, QPalette::WindowText, disabled_text);
  palette.setColor(QPalette::Disabled, QPalette::ButtonText, disabled_text);
  QApplication::setPalette(palette);

  // The stylesheet is a template with @tokens for the theme colors.
  // CUTE_CSS_FILE can point to a stylesheet on disk to tweak it without rebuilding.
  // Note: don't set background-color on QMainWindow in it, that changes its palette and makes StyleSheetLoader loop forever.
  QFile file(qEnvironmentVariableIsSet("CUTE_CSS_FILE") ? qEnvironmentVariable("CUTE_CSS_FILE") : u":/style/cute.css"_s);
  if (!file.open(QIODevice::ReadOnly)) {
    qLog(Error) << "Could not open cute stylesheet:" << file.errorString();
    return;
  }
  QString css = QString::fromUtf8(file.readAll());
  file.close();

  if (c.modern_playlist) {
    QFile modern_file(u":/style/modernplaylist.css"_s);
    if (modern_file.open(QIODevice::ReadOnly)) {
      css += u'\n' + QString::fromUtf8(modern_file.readAll());
      modern_file.close();
    }
  }

  QList<QPair<QString, QString>> tokens = {
    { u"@accentStrong"_s, c.AccentStrong().name() },
    { u"@onAccent"_s, c.OnAccent().name() },
    { u"@accentLight"_s, c.AccentLight().name() },
    { u"@accentSoft"_s, c.AccentSoft().name() },
    { u"@accent2"_s, c.accent2.name() },
    { u"@accent"_s, c.accent.name() },
    { u"@window"_s, c.window.name() },
    { u"@base"_s, c.base.name() },
    { u"@textMuted"_s, c.TextMuted().name() },
    { u"@textDisabled"_s, disabled_text.name() },
    { u"@text"_s, c.text.name() },
    { u"@header"_s, c.Header().name() },
    { u"@radiusLarge"_s, QString::number(c.radius + 2) + u"px"_s },
    { u"@radiusSmall"_s, QString::number(std::max(0, c.radius - 3)) + u"px"_s },
    { u"@radius"_s, QString::number(c.radius) + u"px"_s },
  };
  // Replace longer tokens first so e.g. @accent doesn't eat the start of @accentLight.
  std::stable_sort(tokens.begin(), tokens.end(), [](const QPair<QString, QString> &a, const QPair<QString, QString> &b) { return a.first.length() > b.first.length(); });
  for (const QPair<QString, QString> &token : std::as_const(tokens)) {
    css.replace(token.first, token.second);
  }

  qApp->setStyleSheet(css);

}

}  // namespace CuteTheme
