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
#include <QFormLayout>
#include <QGroupBox>
#include <QComboBox>
#include <QCheckBox>
#include <QPushButton>
#include <QSlider>
#include <QLabel>
#include <QLineEdit>
#include <QTimer>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QIcon>
#include <QColorDialog>
#include <QFileDialog>
#include <QDialogButtonBox>
#include <QScrollArea>
#include <QMessageBox>
#include <QFrame>
#include <QStandardPaths>
#include <QImage>
#include <QImageReader>
#include <QMimeData>
#include <QUrl>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFile>
#include <QFileInfo>

#include "constants/appearancesettings.h"
#include "core/settings.h"
#include "core/iconloader.h"
#include "core/wallpaperengine.h"
#include "collection/albumgridview.h"
#include "wallpaperenginedialog.h"
#include "customizeappearancedialog.h"

using namespace Qt::Literals::StringLiterals;

namespace {

enum class Fit { Original, Fit, Fill, Stretch };

QSlider *MakeSlider(const int min, const int max, QWidget *parent) {
  QSlider *slider = new QSlider(Qt::Horizontal, parent);
  slider->setRange(min, max);
  return slider;
}

QWidget *SliderRow(QSlider *slider, QLabel *label, QWidget *parent) {
  QWidget *row = new QWidget(parent);
  QHBoxLayout *layout = new QHBoxLayout(row);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->addWidget(slider, 1);
  label->setMinimumWidth(44);
  label->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
  layout->addWidget(label);
  return row;
}

}  // namespace

CustomizeAppearanceDialog::CustomizeAppearanceDialog(QWidget *parent)
    : QDialog(parent),
      background_opacity_(AppearanceSettings::kDefaultBackgroundImageOpacityLevel),
      loading_(false),
      preset_(new QComboBox(this)),
      radius_(MakeSlider(0, 20, this)),
      radius_label_(new QLabel(this)),
      background_type_(new QComboBox(this)),
      image_row_(new QWidget(this)),
      image_path_(new QLineEdit(this)),
      image_preview_(new QLabel(this)),
      image_info_(new QLabel(this)),
      opacity_(MakeSlider(0, 100, this)),
      opacity_label_(new QLabel(this)),
      blur_(MakeSlider(0, 50, this)),
      blur_label_(new QLabel(this)),
      fit_(new QComboBox(this)),
      position_(new QComboBox(this)),
      apply_timer_(new QTimer(this)) {

  setWindowTitle(tr("Customize appearance"));
  setWindowIcon(IconLoader::Load(u"view-choose"_s));
  setMinimumWidth(480);
  setAcceptDrops(true);

  // Theme colors
  QGroupBox *colors_group = new QGroupBox(tr("Theme colors"), this);
  QFormLayout *colors_form = new QFormLayout(colors_group);

  const QList<QPair<QString, CuteTheme::Colors>> presets = CuteTheme::Presets();
  for (const QPair<QString, CuteTheme::Colors> &preset : presets) {
    preset_->addItem(preset.first);
  }
  preset_->addItem(tr("Custom"));
  colors_form->addRow(tr("Preset"), preset_);

  color_slots_ = {
    { tr("Accent"), &CuteTheme::Colors::accent, nullptr },
    { tr("Secondary accent"), &CuteTheme::Colors::accent2, nullptr },
    { tr("Window background"), &CuteTheme::Colors::window, nullptr },
    { tr("List background"), &CuteTheme::Colors::base, nullptr },
    { tr("Text"), &CuteTheme::Colors::text, nullptr },
  };
  for (int i = 0; i < color_slots_.count(); ++i) {
    QPushButton *button = new QPushButton(colors_group);
    button->setProperty("slot", i);
    button->setIconSize(QSize(36, 18));
    button->setStyleSheet(u"text-align: left; padding-left: 8px;"_s);
    QObject::connect(button, &QPushButton::clicked, this, &CustomizeAppearanceDialog::PickColor);
    color_slots_[i].button = button;
    colors_form->addRow(color_slots_[i].label, button);
  }

  colors_form->addRow(tr("Corner roundness"), SliderRow(radius_, radius_label_, colors_group));

  modern_playlist_ = new QCheckBox(tr("Modern playlist (album covers, rounded rows and a \"now playing\" card)"), colors_group);
  colors_form->addRow(QString(), modern_playlist_);
  QObject::connect(modern_playlist_, &QCheckBox::toggled, this, [this](const bool checked) {
    colors_.modern_playlist = checked;
    ScheduleApply();
  });

  modern_icons_ = new QCheckBox(tr("Modern monochrome icons (otherwise colorful pastel icons, takes effect after restarting)"), colors_group);
  colors_form->addRow(QString(), modern_icons_);
  QObject::connect(modern_icons_, &QCheckBox::toggled, this, [this](const bool checked) {
    colors_.modern_icons = checked;
    ScheduleApply();
  });

  // Background
  QGroupBox *background_group = new QGroupBox(tr("Playlist background"), this);
  QFormLayout *background_form = new QFormLayout(background_group);

  background_type_->addItem(tr("Kawaii mascot"), static_cast<int>(AppearanceSettings::BackgroundImageType::Default));
  background_type_->addItem(tr("None"), static_cast<int>(AppearanceSettings::BackgroundImageType::None));
  background_type_->addItem(tr("Custom image"), static_cast<int>(AppearanceSettings::BackgroundImageType::Custom));
  background_type_->addItem(tr("Cover of the playing album"), static_cast<int>(AppearanceSettings::BackgroundImageType::Album));
  background_type_->addItem(tr("Hearts and notes pattern"), static_cast<int>(AppearanceSettings::BackgroundImageType::Strawbs));
  background_type_->addItem(tr("Video (animated)"), static_cast<int>(AppearanceSettings::BackgroundImageType::Video));
  background_form->addRow(tr("Background"), background_type_);

  QPushButton *wallpaper_engine = new QPushButton(IconLoader::Load(u"view-choose"_s), tr("Choose from Wallpaper Engine..."), background_group);
  background_form->addRow(QString(), wallpaper_engine);

  QHBoxLayout *image_layout = new QHBoxLayout(image_row_);
  image_layout->setContentsMargins(0, 0, 0, 0);
  image_path_->setPlaceholderText(tr("Choose an image or video..."));
  QPushButton *browse = new QPushButton(tr("Browse..."), image_row_);
  image_layout->addWidget(image_path_, 1);
  image_layout->addWidget(browse);
  background_form->addRow(tr("File"), image_row_);

  // Preview of the chosen image (on a checkerboard so PNG transparency is visible) with its size and format, or why it couldn't be loaded.
  image_preview_->setFixedSize(164, 94);
  image_preview_->setAlignment(Qt::AlignCenter);
  image_info_->setWordWrap(true);
  QWidget *preview_row = new QWidget(background_group);
  QHBoxLayout *preview_layout = new QHBoxLayout(preview_row);
  preview_layout->setContentsMargins(0, 0, 0, 0);
  preview_layout->addWidget(image_preview_);
  preview_layout->addWidget(image_info_, 1);
  background_form->addRow(QString(), preview_row);

  background_form->addRow(tr("Opacity"), SliderRow(opacity_, opacity_label_, background_group));
  background_form->addRow(tr("Blur"), SliderRow(blur_, blur_label_, background_group));

  fit_->addItem(tr("Original size"), static_cast<int>(Fit::Original));
  fit_->addItem(tr("Fit (show the whole image)"), static_cast<int>(Fit::Fit));
  fit_->addItem(tr("Fill (crop to fill)"), static_cast<int>(Fit::Fill));
  fit_->addItem(tr("Stretch"), static_cast<int>(Fit::Stretch));
  background_form->addRow(tr("Size"), fit_);

  position_->addItem(tr("Bottom right"), static_cast<int>(AppearanceSettings::BackgroundImagePosition::BottomRight));
  position_->addItem(tr("Bottom left"), static_cast<int>(AppearanceSettings::BackgroundImagePosition::BottomLeft));
  position_->addItem(tr("Center"), static_cast<int>(AppearanceSettings::BackgroundImagePosition::Middle));
  position_->addItem(tr("Top right"), static_cast<int>(AppearanceSettings::BackgroundImagePosition::UpperRight));
  position_->addItem(tr("Top left"), static_cast<int>(AppearanceSettings::BackgroundImagePosition::UpperLeft));
  background_form->addRow(tr("Position"), position_);

  // Albums tab background, separate from the playlist's
  QGroupBox *grid_group = new QGroupBox(tr("Albums tab background"), this);
  QFormLayout *grid_form = new QFormLayout(grid_group);
  grid_type_ = new QComboBox(grid_group);
  grid_type_->addItem(tr("None"), static_cast<int>(AlbumGridView::BackgroundType::None));
  grid_type_->addItem(tr("Same as the playlist"), static_cast<int>(AlbumGridView::BackgroundType::SameAsPlaylist));
  grid_type_->addItem(tr("Image"), static_cast<int>(AlbumGridView::BackgroundType::Image));
  grid_type_->addItem(tr("Video (animated)"), static_cast<int>(AlbumGridView::BackgroundType::Video));
  grid_form->addRow(tr("Background"), grid_type_);
  QPushButton *grid_wallpaper_engine = new QPushButton(IconLoader::Load(u"view-choose"_s), tr("Choose from Wallpaper Engine..."), grid_group);
  grid_form->addRow(QString(), grid_wallpaper_engine);
  grid_row_ = new QWidget(grid_group);
  QHBoxLayout *grid_row_layout = new QHBoxLayout(grid_row_);
  grid_row_layout->setContentsMargins(0, 0, 0, 0);
  grid_file_ = new QLineEdit(grid_row_);
  grid_file_->setPlaceholderText(tr("Choose an image or video..."));
  QPushButton *grid_browse = new QPushButton(tr("Browse..."), grid_row_);
  grid_row_layout->addWidget(grid_file_, 1);
  grid_row_layout->addWidget(grid_browse);
  grid_form->addRow(tr("File"), grid_row_);
  grid_opacity_ = MakeSlider(0, 100, grid_group);
  grid_opacity_label_ = new QLabel(grid_group);
  grid_form->addRow(tr("Opacity"), SliderRow(grid_opacity_, grid_opacity_label_, grid_group));

  QLabel *hint = new QLabel(tr("Changes are applied right away. Images (PNG, JPEG, WebP, ...) and videos (MP4, WebM, ...) are supported, and you can drop a file on this window for the playlist background. Videos play muted in a loop and pause while the window is minimized."), this);
  hint->setWordWrap(true);
  hint->setEnabled(false);

  QDialogButtonBox *buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
  QPushButton *reset = buttons->addButton(tr("Reset to defaults"), QDialogButtonBox::ResetRole);

  // Scrollable, the dialog doesn't fit on small screens otherwise.
  QWidget *content = new QWidget(this);
  QVBoxLayout *content_layout = new QVBoxLayout(content);
  content_layout->setContentsMargins(0, 0, 6, 0);
  content_layout->addWidget(colors_group);
  content_layout->addWidget(background_group);
  content_layout->addWidget(grid_group);
  content_layout->addWidget(hint);
  QScrollArea *scroll = new QScrollArea(this);
  scroll->setWidget(content);
  scroll->setWidgetResizable(true);
  scroll->setFrameShape(QFrame::NoFrame);
  scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

  QVBoxLayout *layout = new QVBoxLayout(this);
  layout->addWidget(scroll, 1);
  layout->addWidget(buttons);
  resize(560, 860);

  QObject::connect(grid_type_, &QComboBox::currentIndexChanged, this, &CustomizeAppearanceDialog::GridTypeChanged);
  QObject::connect(grid_browse, &QPushButton::clicked, this, [this]() {
    const QString filename = AskMediaFile(grid_file_->text());
    if (!filename.isEmpty()) SetGridFile(filename);
  });
  QObject::connect(grid_wallpaper_engine, &QPushButton::clicked, this, [this]() {
    WallpaperEngineDialog dialog(this);
    if (dialog.exec() == QDialog::Accepted) SetGridFile(dialog.background().file);
  });
  QObject::connect(grid_file_, &QLineEdit::editingFinished, this, &CustomizeAppearanceDialog::ScheduleApply);
  QObject::connect(grid_opacity_, &QSlider::valueChanged, this, [this](const int value) {
    grid_opacity_label_->setText(QString::number(value) + u"%"_s);
    ScheduleApply();
  });

  apply_timer_->setSingleShot(true);
  apply_timer_->setInterval(120);

  QObject::connect(preset_, &QComboBox::activated, this, &CustomizeAppearanceDialog::PresetChanged);
  QObject::connect(radius_, &QSlider::valueChanged, this, [this](const int value) {
    radius_label_->setText(QString::number(value) + u" px"_s);
    colors_.radius = value;
    ScheduleApply();
  });
  QObject::connect(background_type_, &QComboBox::currentIndexChanged, this, &CustomizeAppearanceDialog::BackgroundTypeChanged);
  QObject::connect(browse, &QPushButton::clicked, this, &CustomizeAppearanceDialog::BrowseImage);
  QObject::connect(wallpaper_engine, &QPushButton::clicked, this, &CustomizeAppearanceDialog::ChooseWallpaperEngine);
  QObject::connect(image_path_, &QLineEdit::editingFinished, this, [this]() {
    UpdatePreview();
    ScheduleApply();
  });
  QObject::connect(opacity_, &QSlider::valueChanged, this, &CustomizeAppearanceDialog::OpacityChanged);
  QObject::connect(blur_, &QSlider::valueChanged, this, [this](const int value) {
    blur_label_->setText(QString::number(value));
    ScheduleApply();
  });
  QObject::connect(fit_, &QComboBox::currentIndexChanged, this, [this]() { UpdateBackgroundWidgets(); ScheduleApply(); });
  QObject::connect(position_, &QComboBox::currentIndexChanged, this, &CustomizeAppearanceDialog::ScheduleApply);
  QObject::connect(reset, &QPushButton::clicked, this, &CustomizeAppearanceDialog::ResetToDefaults);
  QObject::connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::close);
  QObject::connect(apply_timer_, &QTimer::timeout, this, &CustomizeAppearanceDialog::Apply);

}

void CustomizeAppearanceDialog::showEvent(QShowEvent *e) {

  Load();
  QDialog::showEvent(e);

}

void CustomizeAppearanceDialog::Load() {

  loading_ = true;

  colors_ = CuteTheme::Load();

  Settings s;
  s.beginGroup(AppearanceSettings::kSettingsGroup);
  const int type = s.value(AppearanceSettings::kBackgroundImageType, static_cast<int>(AppearanceSettings::kDefaultBackgroundImageType)).toInt();
  image_path_->setText(s.value(AppearanceSettings::kBackgroundImageFilename).toString());
  background_opacity_ = s.value(AppearanceSettings::kBackgroundImageOpacityLevel, AppearanceSettings::kDefaultBackgroundImageOpacityLevel).toInt();
  blur_->setValue(s.value(AppearanceSettings::kBackgroundImageBlurRadius, AppearanceSettings::kDefaultBackgroundImageBlurRadius).toInt());
  const bool stretch = s.value(AppearanceSettings::kBackgroundImageStretch, AppearanceSettings::kDefaultBackgroundImageStretch).toBool();
  const bool keep_aspect_ratio = s.value(AppearanceSettings::kBackgroundImageKeepAspectRatio, AppearanceSettings::kDefaultBackgroundImageKeepAspectRatio).toBool();
  const bool do_not_cut = s.value(AppearanceSettings::kBackgroundImageDoNotCut, AppearanceSettings::kDefaultBackgroundImageDoNotCut).toBool();
  const int position = s.value(AppearanceSettings::kBackgroundImagePosition, static_cast<int>(AppearanceSettings::kDefaultBackgroundImagePosition)).toInt();
  s.endGroup();

  Fit fit = Fit::Original;
  if (stretch) {
    if (!keep_aspect_ratio) fit = Fit::Stretch;
    else if (do_not_cut) fit = Fit::Fit;
    else fit = Fit::Fill;
  }

  background_type_->setCurrentIndex(std::max(0, background_type_->findData(type)));
  fit_->setCurrentIndex(std::max(0, fit_->findData(static_cast<int>(fit))));
  position_->setCurrentIndex(std::max(0, position_->findData(position)));
  radius_->setValue(colors_.radius);
  modern_playlist_->setChecked(colors_.modern_playlist);
  modern_icons_->setChecked(colors_.modern_icons);
  radius_label_->setText(QString::number(colors_.radius) + u" px"_s);
  blur_label_->setText(QString::number(blur_->value()));

  s.beginGroup(QLatin1String(AlbumGridView::kSettingsGroup));
  grid_type_->setCurrentIndex(std::max(0, grid_type_->findData(s.value(AlbumGridView::kBackgroundType, static_cast<int>(AlbumGridView::BackgroundType::None)).toInt())));
  grid_file_->setText(s.value(AlbumGridView::kBackgroundFile).toString());
  grid_opacity_->setValue(s.value(AlbumGridView::kBackgroundOpacity, 40).toInt());
  grid_opacity_label_->setText(QString::number(grid_opacity_->value()) + u"%"_s);
  s.endGroup();

  UpdateColorButtons();
  UpdatePresetCombo();
  UpdateBackgroundWidgets();
  UpdateGridWidgets();

  loading_ = false;

}

void CustomizeAppearanceDialog::UpdateColorButtons() {

  for (const ColorSlot &slot : std::as_const(color_slots_)) {
    const QColor color = colors_.*slot.member;
    const qreal dpr = devicePixelRatioF();
    QPixmap swatch(static_cast<int>(36 * dpr), static_cast<int>(18 * dpr));
    swatch.setDevicePixelRatio(dpr);
    swatch.fill(Qt::transparent);
    QPainter p(&swatch);
    p.setRenderHint(QPainter::Antialiasing);
    QPainterPath path;
    path.addRoundedRect(QRectF(0.5, 0.5, 35, 17), 6, 6);
    p.fillPath(path, color);
    p.setPen(QPen(CuteTheme::Mix(color, Qt::black, 0.25), 1));
    p.drawPath(path);
    p.end();
    slot.button->setIcon(QIcon(swatch));
    slot.button->setText(color.name().toUpper());
  }

}

void CustomizeAppearanceDialog::UpdatePresetCombo() {

  const QList<QPair<QString, CuteTheme::Colors>> presets = CuteTheme::Presets();
  int index = preset_->count() - 1;  // Custom
  for (int i = 0; i < presets.count(); ++i) {
    const CuteTheme::Colors &p = presets[i].second;
    if (p.accent == colors_.accent && p.accent2 == colors_.accent2 && p.window == colors_.window && p.base == colors_.base && p.text == colors_.text) {
      index = i;
      break;
    }
  }
  preset_->setCurrentIndex(index);

}

int CustomizeAppearanceDialog::CurrentBackgroundType() const {
  return background_type_->currentData().toInt();
}

void CustomizeAppearanceDialog::UpdateBackgroundWidgets() {

  const AppearanceSettings::BackgroundImageType type = static_cast<AppearanceSettings::BackgroundImageType>(CurrentBackgroundType());
  const bool is_image = type == AppearanceSettings::BackgroundImageType::Custom || type == AppearanceSettings::BackgroundImageType::Album;
  const bool is_video = type == AppearanceSettings::BackgroundImageType::Video;
  const bool is_mascot = type == AppearanceSettings::BackgroundImageType::Default;

  image_row_->setEnabled(type == AppearanceSettings::BackgroundImageType::Custom || is_video);
  opacity_->setEnabled(is_image || is_video || is_mascot);
  blur_->setEnabled(is_image);
  fit_->setEnabled(is_image);  // Videos always fill the background
  const Fit fit = static_cast<Fit>(fit_->currentData().toInt());
  position_->setEnabled(is_image && (fit == Fit::Original || fit == Fit::Fit));

  // The opacity slider edits the mascot opacity or the image/video opacity, depending on the background type.
  const bool was_loading = loading_;
  loading_ = true;
  opacity_->setValue(is_mascot ? colors_.mascot_opacity : background_opacity_);
  opacity_label_->setText(QString::number(opacity_->value()) + u"%"_s);
  loading_ = was_loading;

  UpdatePreview();

}

void CustomizeAppearanceDialog::PresetChanged(const int index) {

  const QList<QPair<QString, CuteTheme::Colors>> presets = CuteTheme::Presets();
  if (index < 0 || index >= presets.count()) return;
  const CuteTheme::Colors &preset = presets[index].second;
  colors_.accent = preset.accent;
  colors_.accent2 = preset.accent2;
  colors_.window = preset.window;
  colors_.base = preset.base;
  colors_.text = preset.text;
  UpdateColorButtons();
  ScheduleApply();

}

void CustomizeAppearanceDialog::PickColor() {

  QPushButton *button = qobject_cast<QPushButton*>(sender());
  if (!button) return;
  const ColorSlot &slot = color_slots_[button->property("slot").toInt()];
  const QColor color = QColorDialog::getColor(colors_.*slot.member, this, slot.label);
  if (!color.isValid()) return;
  colors_.*slot.member = color;
  UpdateColorButtons();
  UpdatePresetCombo();
  ScheduleApply();

}

void CustomizeAppearanceDialog::BackgroundTypeChanged() {

  UpdateBackgroundWidgets();

  if (!loading_) {
    // An image path doesn't work as video and the other way around: ask for a matching file.
    const int type = CurrentBackgroundType();
    const bool wants_image = type == static_cast<int>(AppearanceSettings::BackgroundImageType::Custom);
    const bool wants_video = type == static_cast<int>(AppearanceSettings::BackgroundImageType::Video);
    const QString path = image_path_->text();
    if ((wants_image && (path.isEmpty() || IsVideoFile(path))) || (wants_video && (path.isEmpty() || !IsVideoFile(path)))) {
      BrowseImage();
    }
  }

  ScheduleApply();

}

bool CustomizeAppearanceDialog::IsVideoFile(const QString &filename) {

  static const QStringList video_suffixes = { u"mp4"_s, u"webm"_s, u"mkv"_s, u"mov"_s, u"m4v"_s, u"avi"_s, u"wmv"_s };
  return video_suffixes.contains(QFileInfo(filename).suffix().toLower());

}

void CustomizeAppearanceDialog::BrowseImage() {

  const QString filename = AskMediaFile(image_path_->text());
  if (filename.isEmpty()) return;
  SetBackgroundFile(filename);

}

QString CustomizeAppearanceDialog::AskMediaFile(const QString &current) {

  const QString images = u"*.png *.jpg *.jpeg *.jfif *.webp *.bmp *.gif *.tif *.tiff"_s;
  const QString videos = u"*.mp4 *.webm *.mkv *.mov *.m4v *.avi *.wmv"_s;
  const QString filter = tr("Images and videos (%1 %2)").arg(images, videos) + u";;"_s +
                         tr("Images (%1)").arg(images) + u";;"_s +
                         tr("Videos (%1)").arg(videos) + u";;"_s +
                         tr("All files (*)");

  const QString start = current.isEmpty() ? QStandardPaths::writableLocation(QStandardPaths::PicturesLocation) : current;
  return QFileDialog::getOpenFileName(this, tr("Choose a background image or video"), start, filter);

}

void CustomizeAppearanceDialog::SetGridFile(const QString &filename) {

  const bool video = IsVideoFile(filename);
  QString path = filename;
  if (!video) {
    QString error;
    path = WallpaperEngine::ImportImage(filename, &error);
    if (path.isEmpty()) {
      QMessageBox::warning(this, tr("Albums tab background"), error);
      return;
    }
  }

  loading_ = true;
  grid_file_->setText(path);
  grid_type_->setCurrentIndex(grid_type_->findData(static_cast<int>(video ? AlbumGridView::BackgroundType::Video : AlbumGridView::BackgroundType::Image)));
  loading_ = false;
  UpdateGridWidgets();
  ScheduleApply();

}

void CustomizeAppearanceDialog::UpdateGridWidgets() {

  const AlbumGridView::BackgroundType type = static_cast<AlbumGridView::BackgroundType>(grid_type_->currentData().toInt());
  const bool own = type == AlbumGridView::BackgroundType::Image || type == AlbumGridView::BackgroundType::Video;
  grid_row_->setEnabled(own);
  grid_opacity_->setEnabled(own);

}

void CustomizeAppearanceDialog::GridTypeChanged() {

  UpdateGridWidgets();
  if (!loading_) {
    const AlbumGridView::BackgroundType type = static_cast<AlbumGridView::BackgroundType>(grid_type_->currentData().toInt());
    const QString path = grid_file_->text();
    const bool needs_file = (type == AlbumGridView::BackgroundType::Image && (path.isEmpty() || IsVideoFile(path))) || (type == AlbumGridView::BackgroundType::Video && (path.isEmpty() || !IsVideoFile(path)));
    if (needs_file) {
      const QString filename = AskMediaFile(path);
      if (!filename.isEmpty()) {
        SetGridFile(filename);
        return;
      }
    }
  }
  ScheduleApply();

}

void CustomizeAppearanceDialog::ChooseWallpaperEngine() {

  WallpaperEngineDialog dialog(this);
  if (dialog.exec() != QDialog::Accepted) return;
  // Wallpapers are made to fill the screen, so fill the background with them too.
  fit_->setCurrentIndex(fit_->findData(static_cast<int>(Fit::Fill)));
  SetBackgroundFile(dialog.background().file);

}

void CustomizeAppearanceDialog::SetBackgroundFile(const QString &filename) {

  const bool video = IsVideoFile(filename);
  QString path = filename;

  if (!video) {
    // Copy the image to Strawberry's own folder, so the background doesn't depend on the original file
    // (e.g. an online-only OneDrive file that can't be read when OneDrive isn't running).
    QString error;
    path = WallpaperEngine::ImportImage(filename, &error);
    if (path.isEmpty()) {
      image_preview_->clear();
      image_preview_->setVisible(true);
      image_info_->setVisible(true);
      image_info_->setText(error);
      image_info_->setStyleSheet(u"color: #D0455B;"_s);
      return;
    }
  }

  loading_ = true;
  image_path_->setText(path);
  const AppearanceSettings::BackgroundImageType type = video ? AppearanceSettings::BackgroundImageType::Video : AppearanceSettings::BackgroundImageType::Custom;
  background_type_->setCurrentIndex(background_type_->findData(static_cast<int>(type)));
  loading_ = false;

  UpdateBackgroundWidgets();
  ScheduleApply();

}

void CustomizeAppearanceDialog::UpdatePreview() {

  const int type = CurrentBackgroundType();
  const bool custom = type == static_cast<int>(AppearanceSettings::BackgroundImageType::Custom);
  const bool video = type == static_cast<int>(AppearanceSettings::BackgroundImageType::Video);
  image_preview_->setVisible(custom || video);
  image_info_->setVisible(custom || video);
  if (!custom && !video) return;

  image_info_->setStyleSheet(QString());
  const QString filename = image_path_->text();
  if (filename.isEmpty()) {
    image_preview_->clear();
    image_info_->setText(video ? tr("Choose a video (MP4, WebM, ...) or a Wallpaper Engine wallpaper.") : tr("Choose a PNG, JPEG or WebP image, or drop one on this window."));
    return;
  }

  auto show_error = [this](const QString &message) {
    image_preview_->clear();
    image_info_->setText(message);
    image_info_->setStyleSheet(u"color: #D0455B;"_s);
  };

  QImage image;
  QString info;
  if (video) {
    if (!QFile::exists(filename)) {
      show_error(tr("The video file doesn't exist."));
      return;
    }
    // Wallpaper Engine videos come with a preview picture next to them.
    const QString dir = QFileInfo(filename).absolutePath();
    for (const QString &name : { u"preview.jpg"_s, u"preview.png"_s, u"preview.gif"_s }) {
      if (QFile::exists(dir + u'/' + name)) {
        image = CuteTheme::LoadImageFile(dir + u'/' + name);
        break;
      }
    }
    info = tr("Video \"%1\", plays muted in a loop.").arg(QFileInfo(filename).fileName());
  }
  else {
    QString error;
    image = CuteTheme::LoadImageFile(filename, &error);
    if (image.isNull()) {
      show_error(tr("Couldn't load this image: %1").arg(error));
      return;
    }
    QString format = QString::fromLatin1(QImageReader::imageFormat(filename)).toUpper();
    if (format == "JPG"_L1) format = u"JPEG"_s;
    info = tr("%1 x %2 px").arg(image.width()).arg(image.height());
    if (!format.isEmpty()) info += u", "_s + format;
    if (image.hasAlphaChannel()) info += u", "_s + tr("with transparency");
  }
  image_info_->setText(info);

  if (image.isNull()) {
    image_preview_->clear();
    return;
  }

  const qreal dpr = devicePixelRatioF();
  const QSize box = image_preview_->size() - QSize(4, 4);
  const QImage thumb = image.scaled(box * dpr, Qt::KeepAspectRatio, Qt::SmoothTransformation);
  QPixmap preview(thumb.size());
  preview.setDevicePixelRatio(dpr);
  {
    // Checkerboard behind the image so transparent PNGs are visible as such.
    QPainter p(&preview);
    const int cell = static_cast<int>(6 * dpr);
    for (int y = 0; y < thumb.height(); y += cell) {
      for (int x = 0; x < thumb.width(); x += cell) {
        p.fillRect(x, y, cell, cell, ((x / cell + y / cell) % 2) ? QColor(0xE8, 0xE8, 0xE8) : QColor(Qt::white));
      }
    }
    p.drawImage(0, 0, thumb);
  }
  image_preview_->setPixmap(preview);

}

void CustomizeAppearanceDialog::dragEnterEvent(QDragEnterEvent *e) {

  if (e->mimeData()->hasUrls() && !e->mimeData()->urls().isEmpty() && e->mimeData()->urls().constFirst().isLocalFile()) {
    e->acceptProposedAction();
  }

}

void CustomizeAppearanceDialog::dropEvent(QDropEvent *e) {

  const QString filename = e->mimeData()->urls().constFirst().toLocalFile();
  if (filename.isEmpty()) return;
  SetBackgroundFile(filename);
  e->acceptProposedAction();

}

void CustomizeAppearanceDialog::OpacityChanged(const int value) {

  opacity_label_->setText(QString::number(value) + u"%"_s);
  if (loading_) return;
  if (CurrentBackgroundType() == static_cast<int>(AppearanceSettings::BackgroundImageType::Default)) {
    colors_.mascot_opacity = value;
  }
  else {
    background_opacity_ = value;
  }
  ScheduleApply();

}

void CustomizeAppearanceDialog::ResetToDefaults() {

  loading_ = true;
  colors_ = CuteTheme::Default();
  background_opacity_ = AppearanceSettings::kDefaultBackgroundImageOpacityLevel;
  background_type_->setCurrentIndex(background_type_->findData(static_cast<int>(AppearanceSettings::BackgroundImageType::Default)));
  blur_->setValue(AppearanceSettings::kDefaultBackgroundImageBlurRadius);
  fit_->setCurrentIndex(fit_->findData(static_cast<int>(Fit::Original)));
  position_->setCurrentIndex(position_->findData(static_cast<int>(AppearanceSettings::BackgroundImagePosition::BottomRight)));
  radius_->setValue(colors_.radius);
  modern_playlist_->setChecked(colors_.modern_playlist);
  modern_icons_->setChecked(colors_.modern_icons);
  grid_type_->setCurrentIndex(grid_type_->findData(static_cast<int>(AlbumGridView::BackgroundType::None)));
  grid_opacity_->setValue(40);
  UpdateColorButtons();
  UpdatePresetCombo();
  UpdateBackgroundWidgets();
  UpdateGridWidgets();
  loading_ = false;
  Apply();

}

void CustomizeAppearanceDialog::ScheduleApply() {

  if (loading_) return;
  apply_timer_->start();

}

void CustomizeAppearanceDialog::Apply() {

  CuteTheme::Save(colors_);
  if (colors_ != CuteTheme::Current()) {
    CuteTheme::Apply(colors_);
  }

  const Fit fit = static_cast<Fit>(fit_->currentData().toInt());

  Settings s;
  s.beginGroup(AppearanceSettings::kSettingsGroup);
  s.setValue(AppearanceSettings::kBackgroundImageType, CurrentBackgroundType());
  s.setValue(AppearanceSettings::kBackgroundImageFilename, image_path_->text());
  s.setValue(AppearanceSettings::kBackgroundImageOpacityLevel, background_opacity_);
  s.setValue(AppearanceSettings::kBackgroundImageBlurRadius, blur_->value());
  s.setValue(AppearanceSettings::kBackgroundImageStretch, fit != Fit::Original);
  s.setValue(AppearanceSettings::kBackgroundImageKeepAspectRatio, fit != Fit::Stretch);
  s.setValue(AppearanceSettings::kBackgroundImageDoNotCut, fit != Fit::Fill);
  s.setValue(AppearanceSettings::kBackgroundImagePosition, position_->currentData().toInt());
  s.endGroup();

  s.beginGroup(QLatin1String(AlbumGridView::kSettingsGroup));
  s.setValue(AlbumGridView::kBackgroundType, grid_type_->currentData().toInt());
  s.setValue(AlbumGridView::kBackgroundFile, grid_file_->text());
  s.setValue(AlbumGridView::kBackgroundOpacity, grid_opacity_->value());
  s.endGroup();

  Q_EMIT AppearanceChanged();

}
