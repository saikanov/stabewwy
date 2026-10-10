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

#ifndef NOWPLAYINGCARD_H
#define NOWPLAYINGCARD_H

#include "config.h"

#include <QWidget>
#include <QImage>
#include <QPixmap>
#include <QStringList>

#include "core/song.h"

class QPaintEvent;

// Header card above the playlist (modern playlist look): big cover on a blurred cover background,
// title, artist, album and the audio format of the playing song.
class NowPlayingCard : public QWidget {
  Q_OBJECT

 public:
  explicit NowPlayingCard(QWidget *parent = nullptr);

  QSize sizeHint() const override;

 public Q_SLOTS:
  void SetSong(const Song &song, const QImage &cover);
  void SetPaused(const bool paused);
  void Clear();

 protected:
  void paintEvent(QPaintEvent *e) override;

 private:
  static constexpr int kHeight = 150;
  static constexpr int kCoverSize = 112;

  bool has_song_;
  bool paused_;
  QString title_;
  QString artist_;
  QString details_;
  QStringList chips_;
  QPixmap cover_;
  QImage blurred_;
  QPixmap logo_;
};

#endif  // NOWPLAYINGCARD_H
