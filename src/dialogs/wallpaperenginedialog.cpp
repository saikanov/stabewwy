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

#include <QApplication>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QComboBox>
#include <QLabel>
#include <QListView>
#include <QPushButton>
#include <QDialogButtonBox>
#include <QStandardItemModel>
#include <QSortFilterProxyModel>
#include <QItemSelectionModel>
#include <QTimer>
#include <QImage>
#include <QImageReader>
#include <QPixmap>
#include <QPainter>
#include <QFutureWatcher>
#include <QtConcurrentRun>

#include "core/iconloader.h"
#include "wallpaperenginedialog.h"

using namespace Qt::Literals::StringLiterals;

namespace {

constexpr int kThumbnailWidth = 208;
constexpr int kThumbnailHeight = 117;
constexpr int kThumbnailsPerTick = 4;

class FilterProxy : public QSortFilterProxyModel {
 public:
  explicit FilterProxy(QObject *parent) : QSortFilterProxyModel(parent) {}
  void SetFilter(const QString &text, const QString &type) {
#if QT_VERSION >= QT_VERSION_CHECK(6, 10, 0)
    beginFilterChange();
    text_ = text;
    type_ = type;
    endFilterChange(QSortFilterProxyModel::Direction::Rows);
#else
    text_ = text;
    type_ = type;
    invalidateFilter();
#endif
  }
 protected:
  bool filterAcceptsRow(const int row, const QModelIndex &parent) const override {
    const QModelIndex idx = sourceModel()->index(row, 0, parent);
    if (!type_.isEmpty() && idx.data(Qt::UserRole + 2).toString() != type_) return false;
    return text_.isEmpty() || idx.data(Qt::UserRole + 3).toString().contains(text_, Qt::CaseInsensitive);
  }
 private:
  QString text_;
  QString type_;
};

struct PrepareResult {
  bool success = false;
  WallpaperEngine::Background background;
  QString error;
};

QString TypeLabel(const QString &type) {
  if (type == "video"_L1) return QObject::tr("Video, animated");
  if (type == "scene"_L1) return QObject::tr("Scene, still image");
  if (type == "web"_L1) return QObject::tr("Web, preview image");
  return QObject::tr("Unknown type");
}

}  // namespace

WallpaperEngineDialog::WallpaperEngineDialog(QWidget *parent)
    : QDialog(parent),
      next_thumbnail_(0),
      loaded_(false),
      search_(new QLineEdit(this)),
      type_(new QComboBox(this)),
      status_(new QLabel(this)),
      view_(new QListView(this)),
      model_(new QStandardItemModel(this)),
      proxy_(new FilterProxy(this)),
      use_(nullptr),
      thumbnail_timer_(new QTimer(this)) {

  setWindowTitle(tr("Wallpaper Engine wallpapers"));
  setWindowIcon(IconLoader::Load(u"view-choose"_s));
  resize(960, 640);

  search_->setPlaceholderText(tr("Search wallpapers..."));
  search_->setClearButtonEnabled(true);
  type_->addItem(tr("All types"), QString());
  type_->addItem(tr("Videos (animated)"), u"video"_s);
  type_->addItem(tr("Scenes (still image)"), u"scene"_s);
  type_->addItem(tr("Web (preview image)"), u"web"_s);

  QHBoxLayout *top = new QHBoxLayout;
  top->addWidget(search_, 1);
  top->addWidget(type_);

  proxy_->setSourceModel(model_);
  view_->setModel(proxy_);
  view_->setViewMode(QListView::IconMode);
  view_->setResizeMode(QListView::Adjust);
  view_->setMovement(QListView::Static);
  view_->setUniformItemSizes(true);
  view_->setIconSize(QSize(kThumbnailWidth, kThumbnailHeight));
  view_->setGridSize(QSize(kThumbnailWidth + 16, kThumbnailHeight + 58));
  view_->setWordWrap(true);
  view_->setTextElideMode(Qt::ElideRight);
  view_->setSpacing(4);
  view_->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
  view_->setEditTriggers(QAbstractItemView::NoEditTriggers);

  status_->setWordWrap(true);

  QDialogButtonBox *buttons = new QDialogButtonBox(QDialogButtonBox::Cancel, this);
  use_ = buttons->addButton(tr("Use as background"), QDialogButtonBox::AcceptRole);
  use_->setEnabled(false);

  QVBoxLayout *layout = new QVBoxLayout(this);
  layout->addLayout(top);
  layout->addWidget(view_, 1);
  layout->addWidget(status_);
  layout->addWidget(buttons);

  thumbnail_timer_->setInterval(0);
  QObject::connect(thumbnail_timer_, &QTimer::timeout, this, &WallpaperEngineDialog::LoadNextThumbnails);
  QObject::connect(search_, &QLineEdit::textChanged, this, &WallpaperEngineDialog::UpdateFilter);
  QObject::connect(type_, &QComboBox::currentIndexChanged, this, &WallpaperEngineDialog::UpdateFilter);
  QObject::connect(view_, &QListView::activated, this, &WallpaperEngineDialog::UseSelected);
  QObject::connect(view_->selectionModel(), &QItemSelectionModel::currentChanged, this, [this](const QModelIndex &current) { use_->setEnabled(current.isValid()); });
  QObject::connect(buttons, &QDialogButtonBox::accepted, this, &WallpaperEngineDialog::UseSelected);
  QObject::connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

}

void WallpaperEngineDialog::showEvent(QShowEvent *e) {

  QDialog::showEvent(e);
  if (!loaded_) {
    loaded_ = true;
    // Let the dialog show up first, scanning the Steam folders can take a moment.
    QTimer::singleShot(0, this, &WallpaperEngineDialog::LoadItems);
  }

}

void WallpaperEngineDialog::LoadItems() {

  QApplication::setOverrideCursor(Qt::WaitCursor);
  items_ = WallpaperEngine::FindItems();
  QApplication::restoreOverrideCursor();

  // Placeholder thumbnail until the preview is loaded
  QPixmap placeholder(kThumbnailWidth, kThumbnailHeight);
  placeholder.fill(palette().color(QPalette::AlternateBase));

  for (int i = 0; i < items_.count(); ++i) {
    const WallpaperEngine::Item &item = items_[i];
    QStandardItem *model_item = new QStandardItem(QIcon(placeholder), item.title + u'\n' + TypeLabel(item.type));
    model_item->setData(i, Role_Index);
    model_item->setData(item.type, Role_Type);
    model_item->setData(QString(item.title + u' ' + item.id), Role_Search);
    model_item->setToolTip(item.title + u"\n"_s + TypeLabel(item.type) + u"\n"_s + item.dir);
    model_->appendRow(model_item);
  }

  if (items_.isEmpty()) {
    status_->setText(tr("No Wallpaper Engine wallpapers were found. Subscribe to wallpapers in the Steam Workshop or create them in Wallpaper Engine first."));
  }
  else {
    status_->setText(tr("%n wallpaper(s) found. Videos play animated (muted, in a loop); for scenes the main image is used, since their effects need Wallpaper Engine itself.", "", static_cast<int>(items_.count())));
  }

  next_thumbnail_ = 0;
  thumbnail_timer_->start();

}

void WallpaperEngineDialog::LoadNextThumbnails() {

  for (int n = 0; n < kThumbnailsPerTick && next_thumbnail_ < model_->rowCount(); ++n, ++next_thumbnail_) {
    QStandardItem *model_item = model_->item(next_thumbnail_);
    const WallpaperEngine::Item &item = items_[model_item->data(Role_Index).toInt()];
    if (item.preview.isEmpty()) continue;

    QImageReader reader(item.preview);
    reader.setDecideFormatFromContent(true);
    const QSize size = reader.size();
    if (size.isValid()) {
      // Decode scaled down (fast for JPEG), just big enough to cover the thumbnail.
      reader.setScaledSize(size.scaled(kThumbnailWidth, kThumbnailHeight, Qt::KeepAspectRatioByExpanding));
    }
    QImage image = reader.read();
    if (image.isNull()) continue;
    image = image.scaled(kThumbnailWidth, kThumbnailHeight, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
    image = image.copy((image.width() - kThumbnailWidth) / 2, (image.height() - kThumbnailHeight) / 2, kThumbnailWidth, kThumbnailHeight);
    model_item->setIcon(QIcon(QPixmap::fromImage(image)));
  }

  if (next_thumbnail_ >= model_->rowCount()) thumbnail_timer_->stop();

}

void WallpaperEngineDialog::UpdateFilter() {

  static_cast<FilterProxy*>(proxy_)->SetFilter(search_->text(), type_->currentData().toString());

}

void WallpaperEngineDialog::SetBusy(const bool busy, const QString &message) {

  view_->setEnabled(!busy);
  search_->setEnabled(!busy);
  type_->setEnabled(!busy);
  use_->setEnabled(!busy && view_->currentIndex().isValid());
  status_->setText(message);
  if (busy) QApplication::setOverrideCursor(Qt::BusyCursor);
  else QApplication::restoreOverrideCursor();

}

void WallpaperEngineDialog::UseSelected() {

  const QModelIndex idx = view_->currentIndex();
  if (!idx.isValid() || !view_->isEnabled()) return;
  const WallpaperEngine::Item item = items_[idx.data(Role_Index).toInt()];

  SetBusy(true, item.type == "scene"_L1 ? tr("Extracting the image from \"%1\"...").arg(item.title) : tr("Preparing \"%1\"...").arg(item.title));

  // Extracting big scenes takes a few seconds, do it in the background.
  QFutureWatcher<PrepareResult> *watcher = new QFutureWatcher<PrepareResult>(this);
  QObject::connect(watcher, &QFutureWatcher<PrepareResult>::finished, this, [this, watcher, item]() {
    const PrepareResult result = watcher->result();
    watcher->deleteLater();
    SetBusy(false, QString());
    if (!result.success) {
      status_->setText(tr("Couldn't use \"%1\": %2").arg(item.title, result.error));
      return;
    }
    background_ = result.background;
    accept();
  });
  watcher->setFuture(QtConcurrent::run([item]() {
    PrepareResult result;
    result.success = WallpaperEngine::PrepareBackground(item, &result.background, &result.error);
    return result;
  }));

}
