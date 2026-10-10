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

#ifndef WALLPAPERENGINEDIALOG_H
#define WALLPAPERENGINEDIALOG_H

#include "config.h"

#include <QDialog>
#include <QList>

#include "core/wallpaperengine.h"

class QLineEdit;
class QComboBox;
class QLabel;
class QListView;
class QPushButton;
class QStandardItemModel;
class QSortFilterProxyModel;
class QTimer;
class QModelIndex;

// Lets the user pick one of their installed Wallpaper Engine wallpapers as background.
class WallpaperEngineDialog : public QDialog {
  Q_OBJECT

 public:
  explicit WallpaperEngineDialog(QWidget *parent = nullptr);

  WallpaperEngine::Background background() const { return background_; }

 protected:
  void showEvent(QShowEvent *e) override;

 private Q_SLOTS:
  void LoadNextThumbnails();
  void UpdateFilter();
  void UseSelected();

 private:
  enum Role {
    Role_Index = Qt::UserRole + 1,
    Role_Type,
    Role_Search,
  };

  void LoadItems();
  void SetBusy(const bool busy, const QString &message);

  QList<WallpaperEngine::Item> items_;
  WallpaperEngine::Background background_;
  int next_thumbnail_;
  bool loaded_;

  QLineEdit *search_;
  QComboBox *type_;
  QLabel *status_;
  QListView *view_;
  QStandardItemModel *model_;
  QSortFilterProxyModel *proxy_;
  QPushButton *use_;
  QTimer *thumbnail_timer_;
};

#endif  // WALLPAPERENGINEDIALOG_H
