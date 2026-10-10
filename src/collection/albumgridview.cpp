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
#include <utility>

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QLabel>
#include <QListView>
#include <QMenu>
#include <QAction>
#include <QTimer>
#include <QSortFilterProxyModel>
#include <QItemSelectionModel>
#include <QPainter>
#include <QPainterPath>
#include <QLinearGradient>
#include <QFontMetrics>
#include <QShowEvent>
#include <QMimeData>
#include <QSet>
#include <QUrl>
#include <QSqlDatabase>
#include <QMutexLocker>

#include "includes/qt_blurimage.h"
#include "constants/appearancesettings.h"
#include "core/logging.h"
#include "core/settings.h"
#include "core/cutetheme.h"
#include "core/videobackground.h"
#include "core/iconloader.h"
#include "core/database.h"
#include "core/songmimedata.h"
#include "collection/collectionbackend.h"
#include "collection/collectionquery.h"
#include "covermanager/albumcoverloader.h"
#include "covermanager/albumcoverloaderoptions.h"
#include "covermanager/albumcoverloaderresult.h"
#include "albumgridview.h"

using namespace Qt::Literals::StringLiterals;

namespace {
constexpr int kTileMargin = 8;
constexpr int kCoverRadius = 12;
constexpr int kReloadDelayMs = 1500;
}  // namespace

AlbumGridView::AlbumGridView(QWidget *parent)
    : QWidget(parent),
      filter_(new QLineEdit(this)),
      status_(new QLabel(this)),
      view_(new ListView(this)),
      model_(new Model(this)),
      proxy_(new QSortFilterProxyModel(this)),
      reload_timer_(new QTimer(this)),
      context_menu_(new QMenu(this)),
      own_video_(new VideoBackground(this)),
      dirty_(true) {

  filter_->setPlaceholderText(tr("Search albums or artists..."));
  filter_->setClearButtonEnabled(true);

  QHBoxLayout *top_layout = new QHBoxLayout;
  top_layout->setContentsMargins(6, 6, 6, 2);
  top_layout->addWidget(filter_, 1);
  top_layout->addWidget(status_);

  proxy_->setSourceModel(model_);
  proxy_->setFilterRole(Role_SearchText);
  proxy_->setFilterCaseSensitivity(Qt::CaseInsensitive);

  view_->setModel(proxy_);
  view_->setItemDelegate(new AlbumGridDelegate(this));
  view_->setViewMode(QListView::IconMode);
  view_->setResizeMode(QListView::Adjust);
  view_->setMovement(QListView::Static);
  view_->setWrapping(true);
  view_->setUniformItemSizes(true);
  view_->setSpacing(4);
  view_->setSelectionMode(QAbstractItemView::ExtendedSelection);
  view_->setDragEnabled(true);
  view_->setDragDropMode(QAbstractItemView::DragOnly);
  view_->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
  view_->setFrameShape(QFrame::NoFrame);
  view_->setMouseTracking(true);
  view_->viewport()->setAttribute(Qt::WA_Hover);
  view_->setContextMenuPolicy(Qt::CustomContextMenu);
  view_->setEditTriggers(QAbstractItemView::NoEditTriggers);

  QVBoxLayout *layout = new QVBoxLayout(this);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(0);
  layout->addLayout(top_layout);
  layout->addWidget(view_, 1);

  reload_timer_->setSingleShot(true);
  reload_timer_->setInterval(kReloadDelayMs);

  context_menu_->addAction(IconLoader::Load(u"media-playback-start"_s), tr("Play"), this, [this]() {
    QMimeData *mime = MimeDataForIndexes(SelectedSourceIndexes());
    if (MimeData *mimedata = qobject_cast<MimeData*>(mime)) {
      mimedata->clear_first_ = true;
      mimedata->play_now_ = true;
      mimedata->override_user_settings_ = true;
    }
    if (mime) Q_EMIT AddToPlaylist(mime);
  });
  context_menu_->addAction(IconLoader::Load(u"list-add"_s), tr("Add to playlist"), this, [this]() {
    QMimeData *mime = MimeDataForIndexes(SelectedSourceIndexes());
    if (MimeData *mimedata = qobject_cast<MimeData*>(mime)) {
      mimedata->override_user_settings_ = true;
    }
    if (mime) Q_EMIT AddToPlaylist(mime);
  });
  context_menu_->addAction(IconLoader::Load(u"document-new"_s), tr("Open in new playlist"), this, [this]() {
    const QModelIndexList indexes = SelectedSourceIndexes();
    QMimeData *mime = MimeDataForIndexes(indexes);
    if (MimeData *mimedata = qobject_cast<MimeData*>(mime)) {
      mimedata->open_in_new_playlist_ = true;
      mimedata->play_now_ = true;
      mimedata->override_user_settings_ = true;
      if (indexes.count() == 1) mimedata->name_for_new_playlist_ = indexes.first().data(Role_Album).toString();
    }
    if (mime) Q_EMIT AddToPlaylist(mime);
  });

  CreateNoCoverPixmap();

  QObject::connect(filter_, &QLineEdit::textChanged, this, &AlbumGridView::FilterChanged);
  // activated is emitted on double click and when pressing Enter.
  QObject::connect(view_, &QListView::activated, this, &AlbumGridView::ItemDoubleClicked);
  QObject::connect(view_, &QListView::customContextMenuRequested, this, &AlbumGridView::ShowContextMenu);
  QObject::connect(reload_timer_, &QTimer::timeout, this, &AlbumGridView::Reload);
  auto video_frame_changed = [this]() {
    if (view_->video() && view_->isVisible()) view_->viewport()->update();
  };
  QObject::connect(VideoBackground::Instance(), &VideoBackground::FrameChanged, this, video_frame_changed);
  QObject::connect(own_video_, &VideoBackground::FrameChanged, this, video_frame_changed);

}

void AlbumGridView::Init(const SharedPtr<CollectionBackend> collection_backend, const SharedPtr<AlbumCoverLoader> albumcover_loader) {

  collection_backend_ = collection_backend;
  albumcover_loader_ = albumcover_loader;

  QObject::connect(&*collection_backend_, &CollectionBackend::SongsAdded, this, &AlbumGridView::CollectionChanged);
  QObject::connect(&*collection_backend_, &CollectionBackend::SongsDeleted, this, &AlbumGridView::CollectionChanged);
  QObject::connect(&*collection_backend_, &CollectionBackend::SongsChanged, this, &AlbumGridView::CollectionChanged);
  QObject::connect(&*collection_backend_, &CollectionBackend::DatabaseReset, this, &AlbumGridView::CollectionChanged);
  QObject::connect(&*albumcover_loader_, &AlbumCoverLoader::AlbumCoverLoaded, this, &AlbumGridView::AlbumCoverLoaded);

  ReloadSettings();

}

void AlbumGridView::CreateNoCoverPixmap() {

  // Placeholder for albums without a cover: soft gradient tile in the theme colors with a record on it.
  const CuteTheme::Colors &theme = CuteTheme::Current();
  const qreal dpr = devicePixelRatioF();
  nocover_ = QPixmap(static_cast<int>(kCoverSize * dpr), static_cast<int>(kCoverSize * dpr));
  nocover_.setDevicePixelRatio(dpr);
  nocover_.fill(Qt::transparent);
  QPainter p(&nocover_);
  p.setRenderHint(QPainter::Antialiasing);
  QLinearGradient grad(0, 0, kCoverSize, kCoverSize);
  grad.setColorAt(0, theme.SidebarTop());
  grad.setColorAt(1, theme.SidebarBottom());
  p.fillRect(QRect(0, 0, kCoverSize, kCoverSize), grad);
  const QPixmap record = IconLoader::Load(u"library-music"_s).pixmap(64);
  p.drawPixmap((kCoverSize - 64) / 2, (kCoverSize - 64) / 2, 64, 64, record);

}

void AlbumGridView::ReloadSettings() {

  CreateNoCoverPixmap();
  for (int row = 0; row < model_->rowCount(); ++row) {
    QStandardItem *item = model_->item(row);
    if (!item->data(Role_HasCover).toBool()) item->setData(nocover_, Qt::DecorationRole);
  }

  // The grid's own background: nothing, the playlist's background, or its own image or video.
  Settings s;
  s.beginGroup(QLatin1String(kSettingsGroup));
  const BackgroundType grid_type = static_cast<BackgroundType>(s.value(kBackgroundType, static_cast<int>(BackgroundType::None)).toInt());
  const QString grid_file = s.value(kBackgroundFile).toString();
  const int grid_opacity = s.value(kBackgroundOpacity, 40).toInt();
  s.endGroup();

  s.beginGroup(AppearanceSettings::kSettingsGroup);
  const AppearanceSettings::BackgroundImageType playlist_type = static_cast<AppearanceSettings::BackgroundImageType>(s.value(AppearanceSettings::kBackgroundImageType, static_cast<int>(AppearanceSettings::kDefaultBackgroundImageType)).toInt());
  const QString playlist_file = s.value(AppearanceSettings::kBackgroundImageFilename).toString();
  const int playlist_opacity = s.value(AppearanceSettings::kBackgroundImageOpacityLevel, AppearanceSettings::kDefaultBackgroundImageOpacityLevel).toInt();
  const int playlist_blur = s.value(AppearanceSettings::kBackgroundImageBlurRadius, AppearanceSettings::kDefaultBackgroundImageBlurRadius).toInt();
  s.endGroup();

  QImage image;
  VideoBackground *video = nullptr;
  int opacity = grid_opacity;
  int blur = 0;
  switch (grid_type) {
    case BackgroundType::SameAsPlaylist:
      opacity = playlist_opacity;
      blur = playlist_blur;
      if (playlist_type == AppearanceSettings::BackgroundImageType::Custom && !playlist_file.isEmpty()) {
        image = CuteTheme::LoadImageFile(playlist_file);
      }
      else if (playlist_type == AppearanceSettings::BackgroundImageType::Video) {
        video = VideoBackground::Instance();
      }
      break;
    case BackgroundType::Image:
      if (!grid_file.isEmpty()) image = CuteTheme::LoadImageFile(grid_file);
      break;
    case BackgroundType::Video:
      video = own_video_;
      break;
    case BackgroundType::None:
      break;
  }
  own_video_->SetFile(grid_type == BackgroundType::Video ? grid_file : QString());
  if (!image.isNull()) {
    // Keep it reasonably small, it's scaled to the view anyway.
    if (image.width() > 1600 || image.height() > 1600) image = image.scaled(1600, 1600, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    // Non-premultiplied ARGB32 so the opacity filter below only has to scale the alpha channel (PNG with transparency, JPEG, ...).
    image = image.convertToFormat(QImage::Format_ARGB32);
    uchar *bits = image.bits();
    for (qsizetype i = 0; i < image.sizeInBytes(); i += 4) {
      bits[i + 3] = static_cast<uchar>(bits[i + 3] * (opacity / 100.0));
    }
    if (blur > 0) {
      QImage blurred(image.size(), QImage::Format_ARGB32_Premultiplied);
      blurred.fill(Qt::transparent);
      QPainter blur_painter(&blurred);
      qt_blurImage(&blur_painter, image, blur, true, false);
      blur_painter.end();
      image = blurred;
    }
  }
  view_->SetBackground(image);
  view_->SetVideo(video, opacity);
  view_->viewport()->update();

}

void AlbumGridView::ListView::SetBackground(const QImage &image) {

  background_ = image;
  background_scaled_ = QPixmap();

}

void AlbumGridView::ListView::SetVideo(VideoBackground *video, const int opacity) {

  video_ = video;
  video_opacity_ = opacity;

}

void AlbumGridView::ListView::paintEvent(QPaintEvent *e) {

  if (video_) {
    QPainter p(viewport());
    p.setOpacity(video_opacity_ / 100.0);
    video_->Paint(&p, viewport()->rect());
  }
  else if (!background_.isNull()) {
    const QSize size = viewport()->size();
    if (background_scaled_.isNull() || background_scaled_.size() != size) {
      // Fill the view, cropping what doesn't fit.
      const QImage scaled = background_.scaled(size, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
      background_scaled_ = QPixmap::fromImage(scaled.copy((scaled.width() - size.width()) / 2, (scaled.height() - size.height()) / 2, size.width(), size.height()));
    }
    QPainter p(viewport());
    p.drawPixmap(0, 0, background_scaled_);
  }

  QListView::paintEvent(e);

}

void AlbumGridView::ListView::scrollContentsBy(const int dx, const int dy) {

  QListView::scrollContentsBy(dx, dy);
  // The background stays fixed, so repaint everything instead of scrolling the old pixels.
  if (video_ || !background_.isNull()) viewport()->update();

}

void AlbumGridView::showEvent(QShowEvent *e) {

  QWidget::showEvent(e);
  if (dirty_) Reload();
  Q_EMIT Shown();

}

void AlbumGridView::CollectionChanged() {

  dirty_ = true;
  // Only rebuild while visible, and wait a bit so a running collection scan doesn't cause a reload for every batch.
  if (isVisible()) reload_timer_->start();

}

void AlbumGridView::CancelCoverLoads() {

  if (albumcover_loader_ && !cover_tasks_.isEmpty()) {
    albumcover_loader_->CancelTasks(QSet<quint64>(cover_tasks_.keyBegin(), cover_tasks_.keyEnd()));
  }
  cover_tasks_.clear();

}

void AlbumGridView::Reload() {

  if (!collection_backend_ || !albumcover_loader_) return;

  dirty_ = false;
  reload_timer_->stop();
  CancelCoverLoads();
  model_->clear();

  CollectionBackend::AlbumList albums = collection_backend_->GetAllAlbums();
  std::stable_sort(albums.begin(), albums.end(), [](const CollectionBackend::Album &a, const CollectionBackend::Album &b) {
    return a.album.compare(b.album, Qt::CaseInsensitive) < 0;
  });

  AlbumCoverLoaderOptions cover_options(AlbumCoverLoaderOptions::Option::ScaledImage | AlbumCoverLoaderOptions::Option::PadScaledImage);
  cover_options.types = AlbumCoverLoaderOptions::LoadTypes();
  cover_options.desired_scaled_size = QSize(kCoverSize, kCoverSize);
  cover_options.device_pixel_ratio = devicePixelRatioF();

  for (const CollectionBackend::Album &album : std::as_const(albums)) {
    if (album.album.isEmpty() || album.urls.isEmpty()) continue;

    QStandardItem *item = new QStandardItem;
    item->setData(album.album, Qt::DisplayRole);
    item->setData(nocover_, Qt::DecorationRole);
    item->setData(album.album_artist.isEmpty() ? album.album : album.album_artist + u" - "_s + album.album, Qt::ToolTipRole);
    item->setData(album.album_artist, Role_AlbumArtist);
    item->setData(album.album, Role_Album);
    item->setData(QString(album.album_artist + u' ' + album.album), Role_SearchText);
    item->setData(album.art_embedded, Role_ArtEmbedded);
    item->setData(album.art_automatic, Role_ArtAutomatic);
    item->setData(album.art_manual, Role_ArtManual);
    item->setData(album.art_unset, Role_ArtUnset);
    item->setData(album.urls.constFirst(), Role_FirstUrl);
    item->setData(false, Role_HasCover);
    item->setFlags(Qt::ItemIsSelectable | Qt::ItemIsEnabled | Qt::ItemIsDragEnabled);
    model_->appendRow(item);

    if (album.art_embedded || !album.art_automatic.isEmpty() || !album.art_manual.isEmpty()) {
      const quint64 id = albumcover_loader_->LoadImageAsync(cover_options, album.art_embedded, album.art_automatic, album.art_manual, album.art_unset, album.urls.constFirst());
      cover_tasks_.insert(id, QPersistentModelIndex(item->index()));
    }
  }

  UpdateStatus();

}

void AlbumGridView::AlbumCoverLoaded(const quint64 id, const AlbumCoverLoaderResult &result) {

  if (!cover_tasks_.contains(id)) return;
  const QPersistentModelIndex idx = cover_tasks_.take(id);
  if (!idx.isValid()) return;

  if (result.success && !result.image_scaled.isNull() && result.type != AlbumCoverLoaderResult::Type::Unset) {
    model_->setData(idx, QPixmap::fromImage(result.image_scaled), Qt::DecorationRole);
    model_->setData(idx, true, Role_HasCover);
  }

}

void AlbumGridView::FilterChanged(const QString &text) {

  proxy_->setFilterFixedString(text);
  UpdateStatus();

}

void AlbumGridView::UpdateStatus() {

  const int shown = proxy_->rowCount();
  const int total = model_->rowCount();
  status_->setText(shown == total ? tr("%n album(s)", "", total) : tr("%1 of %2").arg(shown).arg(total));

}

QModelIndexList AlbumGridView::SelectedSourceIndexes() const {

  QModelIndexList ret;
  const QModelIndexList selected = view_->selectionModel()->selectedIndexes();
  for (const QModelIndex &proxy_idx : selected) {
    ret << proxy_->mapToSource(proxy_idx);
  }
  return ret;

}

SongList AlbumGridView::GetSongsInAlbum(const QModelIndex &idx) const {

  SongList ret;

  QMutexLocker l(collection_backend_->db()->Mutex());
  QSqlDatabase db(collection_backend_->db()->Connect());

  CollectionQuery q(db, collection_backend_->songs_table());
  q.SetColumnSpec(Song::kRowIdColumnSpec);
  q.AddWhere(u"album"_s, idx.data(Role_Album).toString());
  q.SetOrderBy(u"disc, track, title"_s);

  const QString albumartist = idx.data(Role_AlbumArtist).toString();
  if (!albumartist.isEmpty()) {
    q.AddWhere(u"effective_albumartist"_s, albumartist);
  }
  q.AddCompilationRequirement(albumartist.isEmpty());

  if (!q.Exec()) return ret;

  while (q.Next()) {
    Song song;
    song.InitFromQuery(q, true);
    ret << song;
  }

  return ret;

}

QMimeData *AlbumGridView::MimeDataForIndexes(const QModelIndexList &indexes) const {

  SongList songs;
  for (const QModelIndex &idx : indexes) {
    songs << GetSongsInAlbum(idx);
  }
  if (songs.isEmpty()) return nullptr;

  SongMimeData *mimedata = new SongMimeData;
  mimedata->backend = collection_backend_;
  mimedata->songs = songs;

  QList<QUrl> urls;
  urls.reserve(songs.count());
  for (const Song &song : std::as_const(songs)) {
    urls << song.url();
  }
  mimedata->setUrls(urls);

  return mimedata;

}

void AlbumGridView::ItemDoubleClicked(const QModelIndex &idx) {

  QMimeData *mime = MimeDataForIndexes(QModelIndexList() << proxy_->mapToSource(idx));
  qLog(Debug) << "Album activated:" << idx.data(Role_AlbumArtist).toString() << idx.data(Role_Album).toString() << "songs:" << (mime ? qobject_cast<SongMimeData*>(mime)->songs.count() : 0);
  if (!mime) return;

  // Replace the current playlist with the album and start playing it.
  if (MimeData *mimedata = qobject_cast<MimeData*>(mime)) {
    mimedata->clear_first_ = true;
    mimedata->play_now_ = true;
    mimedata->override_user_settings_ = true;
  }
  Q_EMIT AddToPlaylist(mime);

}

void AlbumGridView::ShowContextMenu(const QPoint &pos) {

  const QModelIndex idx = view_->indexAt(pos);
  if (!idx.isValid()) return;
  if (!view_->selectionModel()->isSelected(idx)) {
    view_->selectionModel()->select(idx, QItemSelectionModel::ClearAndSelect);
  }
  context_menu_->popup(view_->viewport()->mapToGlobal(pos));

}

QStringList AlbumGridView::Model::mimeTypes() const {
  return QStringList() << u"text/uri-list"_s;
}

QMimeData *AlbumGridView::Model::mimeData(const QModelIndexList &indexes) const {
  return view_->MimeDataForIndexes(indexes);
}

void AlbumGridDelegate::paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &idx) const {

  const CuteTheme::Colors &theme = CuteTheme::Current();

  painter->save();
  painter->setRenderHint(QPainter::Antialiasing);
  painter->setRenderHint(QPainter::SmoothPixmapTransform);

  const QRect tile = option.rect.adjusted(2, 2, -2, -2);
  const bool selected = option.state & QStyle::State_Selected;
  const bool hover = option.state & QStyle::State_MouseOver;

  // Tile background on hover / selection
  if (selected || hover) {
    QPainterPath tile_path;
    tile_path.addRoundedRect(tile, 14, 14);
    painter->fillPath(tile_path, selected ? CuteTheme::Mix(theme.accent, theme.base, 0.62) : theme.AccentLight());
    if (selected) {
      painter->setPen(QPen(theme.accent, 1.5));
      painter->drawPath(tile_path);
    }
  }

  // Cover with a soft shadow and rounded corners
  const int cover_size = AlbumGridView::kCoverSize;
  const QRect cover_rect(tile.left() + (tile.width() - cover_size) / 2, tile.top() + kTileMargin, cover_size, cover_size);

  QPainterPath shadow_path;
  shadow_path.addRoundedRect(cover_rect.translated(0, 3), kCoverRadius, kCoverRadius);
  QColor shadow(theme.AccentStrong());
  shadow.setAlpha(45);
  painter->fillPath(shadow_path, shadow);

  QPainterPath cover_path;
  cover_path.addRoundedRect(cover_rect, kCoverRadius, kCoverRadius);
  painter->save();
  painter->setClipPath(cover_path);
  const QPixmap pixmap = idx.data(Qt::DecorationRole).value<QPixmap>();
  if (!pixmap.isNull()) {
    painter->drawPixmap(cover_rect, pixmap);
  }
  painter->restore();
  painter->setPen(QPen(theme.AccentSoft(), 1.2));
  painter->drawPath(cover_path);

  // Album title and artist
  const int text_left = tile.left() + 6;
  const int text_width = tile.width() - 12;
  QFont title_font = option.font;
  title_font.setBold(true);
  const QFontMetrics title_fm(title_font);
  const QFontMetrics artist_fm(option.font);

  int y = cover_rect.bottom() + 8;
  painter->setFont(title_font);
  painter->setPen(theme.text);
  painter->drawText(QRect(text_left, y, text_width, title_fm.height()), Qt::AlignHCenter | Qt::AlignVCenter, title_fm.elidedText(idx.data(AlbumGridView::Role_Album).toString(), Qt::ElideRight, text_width));

  y += title_fm.height() + 1;
  QString artist = idx.data(AlbumGridView::Role_AlbumArtist).toString();
  if (artist.isEmpty()) artist = tr("Various artists");
  painter->setFont(option.font);
  painter->setPen(theme.TextMuted());
  painter->drawText(QRect(text_left, y, text_width, artist_fm.height()), Qt::AlignHCenter | Qt::AlignVCenter, artist_fm.elidedText(artist, Qt::ElideRight, text_width));

  painter->restore();

}

QSize AlbumGridDelegate::sizeHint(const QStyleOptionViewItem &option, const QModelIndex &idx) const {

  Q_UNUSED(idx)

  QFont title_font = option.font;
  title_font.setBold(true);
  const int text_height = QFontMetrics(title_font).height() + QFontMetrics(option.font).height() + 1;
  return QSize(AlbumGridView::kCoverSize + 28, kTileMargin + AlbumGridView::kCoverSize + 8 + text_height + 12);

}
