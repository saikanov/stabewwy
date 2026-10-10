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

#include <QCoreApplication>
#include <QTimer>
#include <QPainter>
#include <QRect>
#include <QRectF>
#include <QMutexLocker>

#include "constants/appearancesettings.h"
#include "core/logging.h"
#include "core/settings.h"
#include "videobackground.h"

using namespace Qt::Literals::StringLiterals;
using namespace std::chrono_literals;

namespace {
// Frames are scaled down to at most this size: it's only a (mostly translucent) background, and it keeps the conversion cheap.
constexpr int kMaxFrameSize = 1280;
constexpr auto kMinFrameInterval = 33ms;  // ~30 fps
constexpr int kPollIntervalMs = 200;
constexpr qint64 kIdlePauseMs = 2000;
}  // namespace

VideoBackground *VideoBackground::Instance() {

  static VideoBackground *instance = new VideoBackground(qApp);
  return instance;

}

VideoBackground::VideoBackground(QObject *parent)
    : QObject(parent),
      pipeline_(nullptr),
      appsink_(nullptr),
      timer_(new QTimer(this)),
      idle_paused_(false),
      converter_(nullptr) {

  gst_video_info_init(&in_info_);
  gst_video_info_init(&out_info_);

  timer_->setInterval(kPollIntervalMs);
  QObject::connect(timer_, &QTimer::timeout, this, &VideoBackground::PollBus);

}

VideoBackground::~VideoBackground() {
  Stop();
}

void VideoBackground::ReloadSettings() {

  Settings s;
  s.beginGroup(AppearanceSettings::kSettingsGroup);
  const AppearanceSettings::BackgroundImageType type = static_cast<AppearanceSettings::BackgroundImageType>(s.value(AppearanceSettings::kBackgroundImageType, static_cast<int>(AppearanceSettings::kDefaultBackgroundImageType)).toInt());
  const QString filename = s.value(AppearanceSettings::kBackgroundImageFilename).toString();
  s.endGroup();

  SetFile(type == AppearanceSettings::BackgroundImageType::Video ? filename : QString());

}

void VideoBackground::SetFile(const QString &filename) {

  if (filename == filename_ && (pipeline_ || filename.isEmpty())) return;

  Stop();
  filename_ = filename;
  if (!filename_.isEmpty()) Start();

}

void VideoBackground::Start() {

  pipeline_ = gst_pipeline_new("videobackground");
  GstElement *decodebin = gst_element_factory_make("uridecodebin", nullptr);
  appsink_ = gst_element_factory_make("appsink", nullptr);
  if (!pipeline_ || !decodebin || !appsink_) {
    qLog(Error) << "Could not create the video background pipeline";
    if (decodebin) gst_object_unref(decodebin);
    if (appsink_) gst_object_unref(appsink_);
    if (pipeline_) gst_object_unref(pipeline_);
    pipeline_ = nullptr;
    appsink_ = nullptr;
    return;
  }

  gchar *uri = gst_filename_to_uri(filename_.toUtf8().constData(), nullptr);
  g_object_set(decodebin, "uri", uri, nullptr);
  g_free(uri);

  GstCaps *caps = gst_caps_new_empty_simple("video/x-raw");
  g_object_set(appsink_, "caps", caps, "sync", TRUE, "max-buffers", 2U, "drop", TRUE, nullptr);
  gst_caps_unref(caps);

  GstAppSinkCallbacks callbacks{};
  callbacks.new_sample = &VideoBackground::NewSampleCallback;
  gst_app_sink_set_callbacks(GST_APP_SINK(appsink_), &callbacks, this, nullptr);

  gst_bin_add_many(GST_BIN(pipeline_), decodebin, appsink_, nullptr);
  g_signal_connect(decodebin, "pad-added", G_CALLBACK(&VideoBackground::PadAddedCallback), this);

  last_convert_ = std::chrono::steady_clock::time_point();
  idle_paused_ = false;
  last_paint_.start();
  gst_element_set_state(pipeline_, GST_STATE_PLAYING);
  timer_->start();

  qLog(Debug) << "Video background started:" << filename_;

}

void VideoBackground::Stop() {

  timer_->stop();

  if (pipeline_) {
    gst_element_set_state(pipeline_, GST_STATE_NULL);
    gst_object_unref(pipeline_);
    pipeline_ = nullptr;
    appsink_ = nullptr;
  }

  if (converter_) {
    gst_video_converter_free(converter_);
    converter_ = nullptr;
  }
  gst_video_info_init(&in_info_);

  bool had_frame = false;
  {
    QMutexLocker l(&mutex_);
    had_frame = !frame_.isNull();
    frame_ = QImage();
  }
  if (had_frame) Q_EMIT FrameChanged();

}

void VideoBackground::PadAddedCallback(GstElement *element, GstPad *pad, gpointer self) {

  Q_UNUSED(element)

  VideoBackground *instance = static_cast<VideoBackground*>(self);

  GstCaps *caps = gst_pad_get_current_caps(pad);
  if (!caps) caps = gst_pad_query_caps(pad, nullptr);
  const gchar *name = (caps && gst_caps_get_size(caps) > 0) ? gst_structure_get_name(gst_caps_get_structure(caps, 0)) : "";
  const bool is_video = g_str_has_prefix(name, "video/");
  const bool is_audio = g_str_has_prefix(name, "audio/");
  if (caps) gst_caps_unref(caps);

  if (is_video) {
    GstPad *sinkpad = gst_element_get_static_pad(instance->appsink_, "sink");
    if (!gst_pad_is_linked(sinkpad)) gst_pad_link(pad, sinkpad);
    gst_object_unref(sinkpad);
  }
  else if (is_audio) {
    // The background is muted: send audio to a fakesink so the demuxer keeps going.
    GstElement *fakesink = gst_element_factory_make("fakesink", nullptr);
    if (!fakesink) return;
    g_object_set(fakesink, "sync", TRUE, nullptr);
    gst_bin_add(GST_BIN(instance->pipeline_), fakesink);
    gst_element_sync_state_with_parent(fakesink);
    GstPad *sinkpad = gst_element_get_static_pad(fakesink, "sink");
    gst_pad_link(pad, sinkpad);
    gst_object_unref(sinkpad);
  }

}

GstFlowReturn VideoBackground::NewSampleCallback(GstAppSink *appsink, gpointer self) {

  VideoBackground *instance = static_cast<VideoBackground*>(self);
  GstSample *sample = gst_app_sink_pull_sample(appsink);
  if (!sample) return GST_FLOW_OK;

  const auto now = std::chrono::steady_clock::now();
  if (now - instance->last_convert_ >= kMinFrameInterval) {
    instance->last_convert_ = now;
    instance->ConvertSample(sample);
  }
  gst_sample_unref(sample);

  return GST_FLOW_OK;

}

void VideoBackground::ConvertSample(GstSample *sample) {

  GstCaps *caps = gst_sample_get_caps(sample);
  GstBuffer *buffer = gst_sample_get_buffer(sample);
  if (!caps || !buffer) return;

  GstVideoInfo info;
  if (!gst_video_info_from_caps(&info, caps)) return;

  // (Re)create the converter when the input format changes: any decoder output format to BGRx (QImage::Format_RGB32), scaled down.
  if (!converter_ || GST_VIDEO_INFO_FORMAT(&info) != GST_VIDEO_INFO_FORMAT(&in_info_) || GST_VIDEO_INFO_WIDTH(&info) != GST_VIDEO_INFO_WIDTH(&in_info_) || GST_VIDEO_INFO_HEIGHT(&info) != GST_VIDEO_INFO_HEIGHT(&in_info_)) {
    if (converter_) gst_video_converter_free(converter_);
    in_info_ = info;
    const int width = GST_VIDEO_INFO_WIDTH(&info);
    const int height = GST_VIDEO_INFO_HEIGHT(&info);
    const double factor = std::min(1.0, static_cast<double>(kMaxFrameSize) / std::max(width, height));
    const int out_width = std::max(2, static_cast<int>(width * factor) & ~1);
    const int out_height = std::max(2, static_cast<int>(height * factor) & ~1);
    gst_video_info_set_format(&out_info_, GST_VIDEO_FORMAT_BGRx, static_cast<guint>(out_width), static_cast<guint>(out_height));
    // The converter only converts format and size, the frame rate has to match.
    GST_VIDEO_INFO_FPS_N(&out_info_) = GST_VIDEO_INFO_FPS_N(&info);
    GST_VIDEO_INFO_FPS_D(&out_info_) = GST_VIDEO_INFO_FPS_D(&info);
    GstStructure *config = gst_structure_new("GstVideoConverter",
                                             GST_VIDEO_CONVERTER_OPT_RESAMPLER_METHOD, GST_TYPE_VIDEO_RESAMPLER_METHOD, GST_VIDEO_RESAMPLER_METHOD_LINEAR,
                                             GST_VIDEO_CONVERTER_OPT_THREADS, G_TYPE_UINT, 2U,
                                             nullptr);
    converter_ = gst_video_converter_new(&in_info_, &out_info_, config);
    if (!converter_) {
      qLog(Error) << "Can't convert video background frames from" << gst_video_format_to_string(GST_VIDEO_INFO_FORMAT(&info));
      return;
    }
  }

  GstVideoFrame in_frame;
  if (!gst_video_frame_map(&in_frame, &in_info_, buffer, GST_MAP_READ)) return;

  QImage image(GST_VIDEO_INFO_WIDTH(&out_info_), GST_VIDEO_INFO_HEIGHT(&out_info_), QImage::Format_RGB32);
  GstBuffer *out_buffer = gst_buffer_new_wrapped_full(static_cast<GstMemoryFlags>(0), image.bits(), static_cast<gsize>(image.sizeInBytes()), 0, static_cast<gsize>(image.sizeInBytes()), nullptr, nullptr);
  // QImage rows are 32-bit aligned, which matches the default BGRx stride.
  GstVideoFrame out_frame;
  if (gst_video_frame_map(&out_frame, &out_info_, out_buffer, GST_MAP_WRITE)) {
    gst_video_converter_frame(converter_, &in_frame, &out_frame);
    gst_video_frame_unmap(&out_frame);
    {
      QMutexLocker l(&mutex_);
      frame_ = image;
    }
    QMetaObject::invokeMethod(this, &VideoBackground::FrameChanged, Qt::QueuedConnection);
  }
  gst_buffer_unref(out_buffer);
  gst_video_frame_unmap(&in_frame);

}

void VideoBackground::PollBus() {

  if (!pipeline_) return;

  GstBus *bus = gst_element_get_bus(pipeline_);
  while (GstMessage *msg = gst_bus_pop(bus)) {
    switch (GST_MESSAGE_TYPE(msg)) {
      case GST_MESSAGE_EOS:
        // Loop
        gst_element_seek_simple(pipeline_, GST_FORMAT_TIME, static_cast<GstSeekFlags>(GST_SEEK_FLAG_FLUSH | GST_SEEK_FLAG_KEY_UNIT), 0);
        break;
      case GST_MESSAGE_ERROR:{
        GError *error = nullptr;
        gchar *debug = nullptr;
        gst_message_parse_error(msg, &error, &debug);
        qLog(Error) << "Video background error:" << (error ? error->message : "") << (debug ? debug : "");
        if (error) g_error_free(error);
        g_free(debug);
        gst_message_unref(msg);
        gst_object_unref(bus);
        Stop();
        return;
      }
      default:
        break;
    }
    gst_message_unref(msg);
  }
  gst_object_unref(bus);

  // Nothing painted the background for a while (minimized or hidden window): pause until it's painted again.
  if (!idle_paused_ && last_paint_.elapsed() > kIdlePauseMs) {
    idle_paused_ = true;
    gst_element_set_state(pipeline_, GST_STATE_PAUSED);
  }

}

void VideoBackground::Paint(QPainter *painter, const QRect &rect) {

  last_paint_.restart();
  if (idle_paused_ && pipeline_) {
    idle_paused_ = false;
    gst_element_set_state(pipeline_, GST_STATE_PLAYING);
  }

  QImage frame;
  {
    QMutexLocker l(&mutex_);
    frame = frame_;
  }
  if (frame.isNull() || rect.isEmpty()) return;

  // Fill the rect, cropping the frame to the rect's aspect ratio.
  const qreal scale = std::max(static_cast<qreal>(rect.width()) / frame.width(), static_cast<qreal>(rect.height()) / frame.height());
  const qreal source_width = rect.width() / scale;
  const qreal source_height = rect.height() / scale;
  const QRectF source((frame.width() - source_width) / 2.0, (frame.height() - source_height) / 2.0, source_width, source_height);
  painter->drawImage(QRectF(rect), frame, source);

}
