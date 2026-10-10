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

#ifndef VIDEOBACKGROUND_H
#define VIDEOBACKGROUND_H

#include "config.h"

#include <chrono>

#include <gst/gst.h>
#include <gst/app/gstappsink.h>
#include <gst/video/video.h>

#include <QObject>
#include <QString>
#include <QImage>
#include <QMutex>
#include <QElapsedTimer>

class QTimer;
class QPainter;
class QRect;

// Plays a video file muted and in a loop, and hands out its frames to be painted as a background
// (used for Wallpaper Engine video wallpapers). Shared by all views showing the background.
class VideoBackground : public QObject {
  Q_OBJECT

 public:
  // The shared player for the playlist background. Other views can create their own instance.
  static VideoBackground *Instance();
  explicit VideoBackground(QObject *parent = nullptr);
  ~VideoBackground() override;

  // Starts playing the file, or stops when it's empty.
  void SetFile(const QString &filename);
  QString file() const { return filename_; }

  // Reads the appearance settings and plays the video if the background type is Video.
  void ReloadSettings();

  // Paints the current frame filling (and cropping to) rect. Painting also tells the player that the
  // background is visible: playback pauses by itself when nothing painted it for a while (e.g. minimized window).
  void Paint(QPainter *painter, const QRect &rect);

 Q_SIGNALS:
  void FrameChanged();

 private:
  void Start();
  void Stop();
  void PollBus();

  static void PadAddedCallback(GstElement *element, GstPad *pad, gpointer self);
  static GstFlowReturn NewSampleCallback(GstAppSink *appsink, gpointer self);
  void ConvertSample(GstSample *sample);

  QString filename_;
  GstElement *pipeline_;
  GstElement *appsink_;
  QTimer *timer_;
  bool idle_paused_;
  QElapsedTimer last_paint_;

  // Streaming thread only
  GstVideoConverter *converter_;
  GstVideoInfo in_info_;
  GstVideoInfo out_info_;
  std::chrono::steady_clock::time_point last_convert_;

  mutable QMutex mutex_;
  QImage frame_;
};

#endif  // VIDEOBACKGROUND_H
