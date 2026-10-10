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

#ifndef ALBUMGRIDVIEW_H
#define ALBUMGRIDVIEW_H

#include "config.h"

#include <QWidget>
#include <QListView>
#include <QHash>
#include <QImage>
#include <QPixmap>
#include <QPersistentModelIndex>
#include <QStyledItemDelegate>
#include <QStandardItemModel>

#include "includes/shared_ptr.h"
#include "core/song.h"

class QLineEdit;
class QLabel;
class QMenu;
class QMimeData;
class QTimer;
class QSortFilterProxyModel;
class QShowEvent;

class CollectionBackend;
class AlbumCoverLoader;
class VideoBackground;
class AlbumCoverLoaderResult;

// Grid of album covers for the whole collection, MusicBee style.
class AlbumGridView : public QWidget {
  Q_OBJECT

 public:
  explicit AlbumGridView(QWidget *parent = nullptr);

  void Init(const SharedPtr<CollectionBackend> collection_backend, const SharedPtr<AlbumCoverLoader> albumcover_loader);

  // Re-reads the theme colors and the custom background image from the settings.
  void ReloadSettings();

  enum Role {
    Role_AlbumArtist = Qt::UserRole + 1,
    Role_Album,
    Role_SearchText,
    Role_ArtEmbedded,
    Role_ArtAutomatic,
    Role_ArtManual,
    Role_ArtUnset,
    Role_FirstUrl,
    Role_HasCover,
  };

  static constexpr int kCoverSize = 220;  // Maximum cover size, tiles get smaller to fit the width of the view

  // The album grid has its own background, set in "Customize appearance".
  static constexpr char kSettingsGroup[] = "AlbumGrid";
  static constexpr char kBackgroundType[] = "background_type";
  static constexpr char kBackgroundFile[] = "background_file";
  static constexpr char kBackgroundOpacity[] = "background_opacity";
  enum class BackgroundType {
    None,
    SameAsPlaylist,
    Image,
    Video
  };

 Q_SIGNALS:
  void AddToPlaylist(QMimeData *mimedata);
  void Shown();

 protected:
  void showEvent(QShowEvent *e) override;

 private Q_SLOTS:
  void CollectionChanged();
  void Reload();
  void AlbumCoverLoaded(const quint64 id, const AlbumCoverLoaderResult &result);
  void FilterChanged(const QString &text);
  void ItemDoubleClicked(const QModelIndex &idx);
  void ShowContextMenu(const QPoint &pos);

 private:
  // List view that paints the custom background image (filled and fixed) behind the tiles.
  class ListView : public QListView {
   public:
    explicit ListView(QWidget *parent) : QListView(parent) {}
    void SetBackground(const QImage &image);
    // Shows a looping video background instead of an image (nullptr for none).
    void SetVideo(VideoBackground *video, const int opacity);
    VideoBackground *video() const { return video_; }
   protected:
    void paintEvent(QPaintEvent *e) override;
    void scrollContentsBy(const int dx, const int dy) override;
    void resizeEvent(QResizeEvent *e) override;
    bool viewportEvent(QEvent *e) override;
   public:
    // Splits the width of the view in equal columns (at least two), so the tiles fill it without a gap on the side.
    void UpdateTileSize();
   protected:
   private:
    QImage background_;
    QPixmap background_scaled_;
    VideoBackground *video_ = nullptr;
    int video_opacity_ = 40;
  };

  class Model : public QStandardItemModel {
   public:
    explicit Model(AlbumGridView *view) : QStandardItemModel(view), view_(view) {}
    QStringList mimeTypes() const override;
    QMimeData *mimeData(const QModelIndexList &indexes) const override;
   private:
    AlbumGridView *view_;
  };

  SongList GetSongsInAlbum(const QModelIndex &idx) const;
  QMimeData *MimeDataForIndexes(const QModelIndexList &indexes) const;
  QModelIndexList SelectedSourceIndexes() const;
  void CancelCoverLoads();
  void UpdateStatus();
  void CreateNoCoverPixmap();

  SharedPtr<CollectionBackend> collection_backend_;
  SharedPtr<AlbumCoverLoader> albumcover_loader_;

  QLineEdit *filter_;
  QLabel *status_;
  ListView *view_;
  Model *model_;
  QSortFilterProxyModel *proxy_;
  QTimer *reload_timer_;
  QMenu *context_menu_;

  QPixmap nocover_;
  VideoBackground *own_video_;  // Video playing when the grid has its own video background
  QHash<quint64, QPersistentModelIndex> cover_tasks_;
  bool dirty_;
};

// Paints one album tile: rounded cover with the album title and artist underneath.
class AlbumGridDelegate : public QStyledItemDelegate {
  Q_OBJECT

 public:
  explicit AlbumGridDelegate(QObject *parent = nullptr) : QStyledItemDelegate(parent) {}
  void paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &idx) const override;
  QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &idx) const override;
};

#endif  // ALBUMGRIDVIEW_H
