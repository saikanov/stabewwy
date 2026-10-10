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

#ifndef CUSTOMIZEAPPEARANCEDIALOG_H
#define CUSTOMIZEAPPEARANCEDIALOG_H

#include "config.h"

#include <QDialog>
#include <QColor>
#include <QList>

#include "core/cutetheme.h"

class QComboBox;
class QCheckBox;
class QPushButton;
class QSlider;
class QLabel;
class QLineEdit;
class QTimer;
class QWidget;
class QDragEnterEvent;
class QDropEvent;

// Live theme editor: colors, corner roundness and the playlist / album grid background.
class CustomizeAppearanceDialog : public QDialog {
  Q_OBJECT

 public:
  explicit CustomizeAppearanceDialog(QWidget *parent = nullptr);

 Q_SIGNALS:
  void AppearanceChanged();

 protected:
  void showEvent(QShowEvent *e) override;
  void dragEnterEvent(QDragEnterEvent *e) override;
  void dropEvent(QDropEvent *e) override;

 private Q_SLOTS:
  void PresetChanged(const int index);
  void PickColor();
  void BackgroundTypeChanged();
  void BrowseImage();
  void ChooseWallpaperEngine();
  void OpacityChanged(const int value);
  void ResetToDefaults();
  void ScheduleApply();
  void Apply();

 private:
  struct ColorSlot {
    QString label;
    QColor CuteTheme::Colors::*member;
    QPushButton *button;
  };

  void Load();
  void UpdateColorButtons();
  void UpdatePresetCombo();
  void UpdateBackgroundWidgets();
  void UpdatePreview();
  // Uses an image (copied to Strawberry's folder) or a video file as background.
  void SetBackgroundFile(const QString &filename);
  static bool IsVideoFile(const QString &filename);
  QString AskMediaFile(const QString &current);
  // Albums tab background
  void SetGridFile(const QString &filename);
  void UpdateGridWidgets();
  void GridTypeChanged();
  int CurrentBackgroundType() const;

  CuteTheme::Colors colors_;
  int background_opacity_;  // opacity_level for custom images and album covers
  bool loading_;

  QComboBox *preset_;
  QList<ColorSlot> color_slots_;
  QSlider *radius_;
  QLabel *radius_label_;
  QCheckBox *modern_playlist_ = nullptr;
  QCheckBox *modern_icons_ = nullptr;

  QComboBox *grid_type_ = nullptr;
  QWidget *grid_row_ = nullptr;
  QLineEdit *grid_file_ = nullptr;
  QSlider *grid_opacity_ = nullptr;
  QLabel *grid_opacity_label_ = nullptr;

  QComboBox *background_type_;
  QWidget *image_row_;
  QLineEdit *image_path_;
  QLabel *image_preview_;
  QLabel *image_info_;
  QSlider *opacity_;
  QLabel *opacity_label_;
  QSlider *blur_;
  QLabel *blur_label_;
  QComboBox *fit_;
  QComboBox *position_;

  QTimer *apply_timer_;
};

#endif  // CUSTOMIZEAPPEARANCEDIALOG_H
