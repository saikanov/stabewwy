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

#include <QPainter>
#include <QPainterPath>
#include <QLinearGradient>
#include <QFontMetrics>
#include <QPaintEvent>

#include "includes/qt_blurimage.h"
#include "core/cutetheme.h"
#include "core/iconloader.h"
#include "nowplayingcard.h"

using namespace Qt::Literals::StringLiterals;

NowPlayingCard::NowPlayingCard(QWidget *parent)
    : QWidget(parent),
      has_song_(false),
      paused_(false),
      logo_(IconLoader::Load(u"strawberry"_s).pixmap(kCoverSize)) {

  setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
  setMinimumHeight(kHeight);
  setMaximumHeight(kHeight);
  Clear();

}

QSize NowPlayingCard::sizeHint() const {
  return QSize(400, kHeight);
}

void NowPlayingCard::SetSong(const Song &song, const QImage &cover) {

  has_song_ = song.is_valid();
  if (!has_song_) {
    Clear();
    return;
  }

  title_ = song.PrettyTitle();
  artist_ = song.artist().isEmpty() ? song.effective_albumartist() : song.artist();
  QStringList details;
  if (!song.album().isEmpty()) details << song.album();
  if (song.year() > 0) details << QString::number(song.year());
  details_ = details.join(u"  ·  "_s);

  chips_.clear();
  const QString filetype = song.TextForFiletype();
  if (!filetype.isEmpty() && filetype != "Unknown"_L1) chips_ << filetype;
  if (song.bitdepth() > 0 && song.samplerate() > 0) {
    chips_ << tr("%1-bit / %2 kHz").arg(song.bitdepth()).arg(song.samplerate() / 1000.0, 0, 'g', 3);
  }
  else if (song.samplerate() > 0) {
    chips_ << tr("%1 kHz").arg(song.samplerate() / 1000.0, 0, 'g', 3);
  }
  if (song.bitrate() > 0) chips_ << tr("%1 kbps").arg(song.bitrate());

  if (cover.isNull()) {
    cover_ = QPixmap();
    blurred_ = QImage();
  }
  else {
    const qreal dpr = devicePixelRatioF();
    QImage scaled = cover.scaled(QSize(kCoverSize, kCoverSize) * dpr, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
    cover_ = QPixmap::fromImage(scaled);
    cover_.setDevicePixelRatio(dpr);

    // Background: the cover, small and heavily blurred
    QImage small = cover.scaled(240, 240, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation).convertToFormat(QImage::Format_ARGB32_Premultiplied);
    blurred_ = QImage(small.size(), QImage::Format_ARGB32_Premultiplied);
    blurred_.fill(Qt::transparent);
    QPainter blur_painter(&blurred_);
    qt_blurImage(&blur_painter, small, 28, true, false);
    blur_painter.end();
  }

  update();

}

void NowPlayingCard::SetPaused(const bool paused) {

  paused_ = paused;
  update();

}

void NowPlayingCard::Clear() {

  has_song_ = false;
  paused_ = false;
  title_ = tr("Nothing playing");
  artist_ = tr("Double-click a song, or an album in the Albums tab, to start listening");
  details_.clear();
  chips_.clear();
  cover_ = QPixmap();
  blurred_ = QImage();
  update();

}

void NowPlayingCard::paintEvent(QPaintEvent *e) {

  Q_UNUSED(e)

  const CuteTheme::Colors &theme = CuteTheme::Current();

  QPainter p(this);
  p.setRenderHint(QPainter::Antialiasing);
  p.setRenderHint(QPainter::SmoothPixmapTransform);

  const QRectF card = QRectF(rect()).adjusted(8, 8, -8, -4);
  const qreal radius = theme.radius + 6;
  QPainterPath card_path;
  card_path.addRoundedRect(card, radius, radius);

  // Background: blurred cover washed with the theme, or a soft accent gradient
  p.save();
  p.setClipPath(card_path);
  if (!blurred_.isNull()) {
    const QSizeF size = QSizeF(blurred_.size()).scaled(card.size(), Qt::KeepAspectRatioByExpanding);
    p.drawImage(QRectF(card.center().x() - size.width() / 2, card.center().y() - size.height() / 2, size.width(), size.height()), blurred_);
    QColor wash(theme.base);
    wash.setAlpha(theme.IsDark() ? 150 : 165);
    p.fillRect(card, wash);
  }
  else {
    QColor start(theme.accent);
    start.setAlpha(70);
    QColor end(theme.accent2);
    end.setAlpha(45);
    QLinearGradient gradient(card.topLeft(), card.bottomRight());
    gradient.setColorAt(0, start);
    gradient.setColorAt(1, end);
    p.fillRect(card, gradient);
  }
  p.restore();
  QColor border(theme.accent);
  border.setAlpha(70);
  p.setPen(QPen(border, 1));
  p.drawPath(card_path);

  // Cover
  const QRectF cover_rect(card.left() + 16, card.center().y() - kCoverSize / 2.0, kCoverSize, kCoverSize);
  QPainterPath cover_path;
  cover_path.addRoundedRect(cover_rect, 12, 12);
  QColor shadow(0, 0, 0, theme.IsDark() ? 90 : 45);
  QPainterPath shadow_path;
  shadow_path.addRoundedRect(cover_rect.translated(0, 4), 12, 12);
  p.fillPath(shadow_path, shadow);
  if (!cover_.isNull()) {
    p.save();
    p.setClipPath(cover_path);
    const QSizeF size = QSizeF(cover_.size() / cover_.devicePixelRatio()).scaled(cover_rect.size(), Qt::KeepAspectRatioByExpanding);
    p.drawPixmap(QRectF(cover_rect.center().x() - size.width() / 2, cover_rect.center().y() - size.height() / 2, size.width(), size.height()), cover_, QRectF(cover_.rect()));
    p.restore();
  }
  else {
    QLinearGradient gradient(cover_rect.topLeft(), cover_rect.bottomRight());
    gradient.setColorAt(0, theme.SidebarTop());
    gradient.setColorAt(1, theme.SidebarBottom());
    p.fillPath(cover_path, gradient);
    const QRectF logo_rect(cover_rect.center().x() - 36, cover_rect.center().y() - 36, 72, 72);
    p.drawPixmap(logo_rect, logo_, QRectF(logo_.rect()));
  }

  // Texts
  const qreal text_left = cover_rect.right() + 22;
  const int text_width = std::max(0, static_cast<int>(card.right() - 20 - text_left));

  QFont label_font = font();
  label_font.setBold(true);
  label_font.setPointSizeF(font().pointSizeF() - 1);
  label_font.setLetterSpacing(QFont::AbsoluteSpacing, 1.5);
  QFont title_font = font();
  title_font.setBold(true);
  title_font.setPointSizeF(font().pointSizeF() + 8);
  QFont artist_font = font();
  artist_font.setPointSizeF(font().pointSizeF() + 1.5);
  QFont details_font = font();
  QFont chip_font = font();
  chip_font.setBold(true);
  chip_font.setPointSizeF(font().pointSizeF() - 1);

  const QFontMetrics label_fm(label_font), title_fm(title_font), artist_fm(artist_font), details_fm(details_font), chip_fm(chip_font);
  const int chips_height = chips_.isEmpty() ? 0 : chip_fm.height() + 8;
  const int total = label_fm.height() + 2 + title_fm.height() + artist_fm.height() + (details_.isEmpty() ? 0 : details_fm.height()) + (chips_.isEmpty() ? 0 : chips_height + 8);
  qreal y = card.center().y() - total / 2.0;

  if (has_song_) {
    p.setFont(label_font);
    p.setPen(theme.AccentStrong());
    p.drawText(QRectF(text_left, y, text_width, label_fm.height()), Qt::AlignLeft | Qt::AlignVCenter, paused_ ? tr("PAUSED") : tr("NOW PLAYING"));
  }
  y += label_fm.height() + 2;

  p.setFont(title_font);
  p.setPen(theme.text);
  p.drawText(QRectF(text_left, y, text_width, title_fm.height()), Qt::AlignLeft | Qt::AlignVCenter, title_fm.elidedText(title_, Qt::ElideRight, text_width));
  y += title_fm.height();

  p.setFont(artist_font);
  p.setPen(has_song_ ? theme.text : theme.TextMuted());
  p.drawText(QRectF(text_left, y, text_width, artist_fm.height()), Qt::AlignLeft | Qt::AlignVCenter, artist_fm.elidedText(artist_, Qt::ElideRight, text_width));
  y += artist_fm.height();

  if (!details_.isEmpty()) {
    p.setFont(details_font);
    p.setPen(theme.TextMuted());
    p.drawText(QRectF(text_left, y, text_width, details_fm.height()), Qt::AlignLeft | Qt::AlignVCenter, details_fm.elidedText(details_, Qt::ElideRight, text_width));
    y += details_fm.height();
  }

  // Format chips
  if (!chips_.isEmpty()) {
    y += 8;
    qreal x = text_left;
    p.setFont(chip_font);
    for (const QString &chip : std::as_const(chips_)) {
      const qreal width = chip_fm.horizontalAdvance(chip) + 18;
      if (x + width > text_left + text_width) break;
      const QRectF chip_rect(x, y, width, chips_height);
      QColor chip_background(theme.accent);
      chip_background.setAlpha(theme.IsDark() ? 70 : 55);
      p.setPen(Qt::NoPen);
      p.setBrush(chip_background);
      p.drawRoundedRect(chip_rect, chips_height / 2.0, chips_height / 2.0);
      p.setPen(theme.text);
      p.drawText(chip_rect, Qt::AlignCenter, chip);
      x += width + 6;
    }
  }

}
