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
#include <cstring>
#include <utility>

#include <QObject>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QSet>
#include <QSettings>
#include <QStandardPaths>
#include <QRegularExpression>
#include <QJsonDocument>
#include <QJsonObject>
#include <QCryptographicHash>
#include <QImageReader>

#include <QBuffer>

#ifdef Q_OS_WIN32
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <windows.h>
#endif

#include "core/logging.h"
#include "wallpaperengine.h"

using namespace Qt::Literals::StringLiterals;

namespace WallpaperEngine {

namespace {

constexpr char kAppId[] = "431960";
constexpr int kMaxSavedImageSize = 3840;

// Little endian reader over a byte array that stops (ok() == false) instead of reading past the end.
class Reader {
 public:
  explicit Reader(const QByteArray &data) : data_(data), pos_(0), ok_(true) {}
  bool ok() const { return ok_; }
  qint64 pos() const { return pos_; }

  qint32 Int32() {
    if (!Need(4)) return 0;
    qint32 value = 0;
    std::memcpy(&value, data_.constData() + pos_, 4);
    pos_ += 4;
    return value;
  }

  QByteArray CString(const int max_length) {
    const qint64 end = data_.indexOf('\0', pos_);
    if (end < 0 || end - pos_ > max_length) {
      ok_ = false;
      return QByteArray();
    }
    const QByteArray value = data_.mid(pos_, end - pos_);
    pos_ = end + 1;
    return value;
  }

  QByteArray Bytes(const qint64 count) {
    if (count < 0 || !Need(count)) return QByteArray();
    const QByteArray value = data_.mid(pos_, count);
    pos_ += count;
    return value;
  }

 private:
  bool Need(const qint64 count) {
    if (!ok_ || pos_ + count > data_.size()) ok_ = false;
    return ok_;
  }

  const QByteArray &data_;
  qint64 pos_;
  bool ok_;
};

// LZ4 block format decompression.
QByteArray Lz4Decompress(const QByteArray &input, const int output_size) {

  if (output_size <= 0) return QByteArray();
  QByteArray output(output_size, Qt::Uninitialized);
  const uchar *ip = reinterpret_cast<const uchar*>(input.constData());
  const uchar *const iend = ip + input.size();
  uchar *op = reinterpret_cast<uchar*>(output.data());
  uchar *const ostart = op;
  uchar *const oend = op + output_size;

  while (ip < iend) {
    const uchar token = *ip++;
    qint64 literal_length = token >> 4;
    if (literal_length == 15) {
      uchar b = 0;
      do {
        if (ip >= iend) return QByteArray();
        b = *ip++;
        literal_length += b;
      } while (b == 255);
    }
    if (literal_length > iend - ip || literal_length > oend - op) return QByteArray();
    std::memcpy(op, ip, static_cast<size_t>(literal_length));
    ip += literal_length;
    op += literal_length;
    if (ip >= iend) break;  // The last sequence only has literals

    if (iend - ip < 2) return QByteArray();
    const qint64 offset = ip[0] | (ip[1] << 8);
    ip += 2;
    if (offset == 0 || offset > op - ostart) return QByteArray();
    qint64 match_length = token & 15;
    if (match_length == 15) {
      uchar b = 0;
      do {
        if (ip >= iend) return QByteArray();
        b = *ip++;
        match_length += b;
      } while (b == 255);
    }
    match_length += 4;
    if (match_length > oend - op) return QByteArray();
    const uchar *match = op - offset;
    for (qint64 i = 0; i < match_length; ++i) *op++ = *match++;  // Byte by byte: the match may overlap the output
  }

  if (op != oend) output.truncate(static_cast<qsizetype>(op - ostart));
  return output;

}

QRgb Rgb565(const quint16 c) {
  const int r = (c >> 11) & 31, g = (c >> 5) & 63, b = c & 31;
  return qRgb((r << 3) | (r >> 2), (g << 2) | (g >> 4), (b << 3) | (b >> 2));
}

// Decodes DXT1 (S3TC/BC1), DXT3 (BC2) and DXT5 (BC3) compressed textures.
QImage DecodeDxt(const QByteArray &data, const int width, const int height, const int dxt) {

  const int blocks_x = (width + 3) / 4;
  const int blocks_y = (height + 3) / 4;
  const int block_size = dxt == 1 ? 8 : 16;
  if (data.size() < static_cast<qint64>(blocks_x) * blocks_y * block_size) return QImage();

  QImage image(width, height, QImage::Format_ARGB32);
  const uchar *src = reinterpret_cast<const uchar*>(data.constData());

  for (int by = 0; by < blocks_y; ++by) {
    for (int bx = 0; bx < blocks_x; ++bx) {
      const uchar *block = src + (static_cast<qint64>(by) * blocks_x + bx) * block_size;

      int alpha[16];
      std::fill(std::begin(alpha), std::end(alpha), 255);
      const uchar *color_block = block;
      if (dxt == 3) {
        for (int i = 0; i < 16; ++i) {
          const int nibble = (block[i / 2] >> ((i % 2) * 4)) & 15;
          alpha[i] = nibble * 17;
        }
        color_block = block + 8;
      }
      else if (dxt == 5) {
        int a[8];
        a[0] = block[0];
        a[1] = block[1];
        if (a[0] > a[1]) {
          for (int i = 1; i < 7; ++i) a[i + 1] = ((7 - i) * a[0] + i * a[1]) / 7;
        }
        else {
          for (int i = 1; i < 5; ++i) a[i + 1] = ((5 - i) * a[0] + i * a[1]) / 5;
          a[6] = 0;
          a[7] = 255;
        }
        quint64 bits = 0;
        for (int i = 0; i < 6; ++i) bits |= static_cast<quint64>(block[2 + i]) << (8 * i);
        for (int i = 0; i < 16; ++i) alpha[i] = a[(bits >> (3 * i)) & 7];
        color_block = block + 8;
      }

      const quint16 c0 = static_cast<quint16>(color_block[0] | (color_block[1] << 8));
      const quint16 c1 = static_cast<quint16>(color_block[2] | (color_block[3] << 8));
      QRgb colors[4];
      colors[0] = Rgb565(c0);
      colors[1] = Rgb565(c1);
      const bool four_colors = dxt != 1 || c0 > c1;
      if (four_colors) {
        colors[2] = qRgb((2 * qRed(colors[0]) + qRed(colors[1])) / 3, (2 * qGreen(colors[0]) + qGreen(colors[1])) / 3, (2 * qBlue(colors[0]) + qBlue(colors[1])) / 3);
        colors[3] = qRgb((qRed(colors[0]) + 2 * qRed(colors[1])) / 3, (qGreen(colors[0]) + 2 * qGreen(colors[1])) / 3, (qBlue(colors[0]) + 2 * qBlue(colors[1])) / 3);
      }
      else {
        colors[2] = qRgb((qRed(colors[0]) + qRed(colors[1])) / 2, (qGreen(colors[0]) + qGreen(colors[1])) / 2, (qBlue(colors[0]) + qBlue(colors[1])) / 2);
        colors[3] = qRgba(0, 0, 0, 0);
      }
      const quint32 indices = color_block[4] | (color_block[5] << 8) | (color_block[6] << 16) | (static_cast<quint32>(color_block[7]) << 24);

      for (int py = 0; py < 4; ++py) {
        const int y = by * 4 + py;
        if (y >= height) break;
        QRgb *line = reinterpret_cast<QRgb*>(image.scanLine(y));
        for (int px = 0; px < 4; ++px) {
          const int x = bx * 4 + px;
          if (x >= width) break;
          const int i = py * 4 + px;
          const int index = (indices >> (2 * i)) & 3;
          const QRgb c = colors[index];
          const int a = (!four_colors && index == 3) ? 0 : alpha[i];
          line[x] = qRgba(qRed(c), qGreen(c), qBlue(c), a);
        }
      }
    }
  }

  return image;

}

struct TexHeader {
  int format = 0;
  int texture_width = 0;
  int texture_height = 0;
  int image_width = 0;
  int image_height = 0;
};

bool ReadTexHeader(Reader *r, TexHeader *header) {

  const QByteArray texv = r->CString(16);
  if (!texv.startsWith("TEXV")) return false;
  r->CString(16);  // TEXI0001
  header->format = r->Int32();
  r->Int32();  // flags
  header->texture_width = r->Int32();
  header->texture_height = r->Int32();
  header->image_width = r->Int32();
  header->image_height = r->Int32();
  r->Int32();  // unknown
  return r->ok();

}

struct PkgEntry {
  QString name;
  qint64 offset = 0;
  qint64 size = 0;
};

// Reads the file table of a scene.pkg. Entry offsets are made absolute.
bool ReadPkg(QFile *file, QList<PkgEntry> *entries) {

  const QByteArray header = file->read(4 * 1024 * 1024);  // The file table is at the start, this is plenty
  Reader r(header);
  const qint32 magic_length = r.Int32();
  if (magic_length <= 0 || magic_length > 64) return false;
  if (!r.Bytes(magic_length).startsWith("PKGV")) return false;
  const qint32 count = r.Int32();
  if (count < 0 || count > 100000) return false;
  for (qint32 i = 0; i < count && r.ok(); ++i) {
    PkgEntry entry;
    const qint32 name_length = r.Int32();
    if (name_length < 0 || name_length > 4096) return false;
    entry.name = QString::fromUtf8(r.Bytes(name_length));
    entry.offset = r.Int32();
    entry.size = r.Int32();
    entries->append(entry);
  }
  if (!r.ok()) return false;
  const qint64 data_start = r.pos();
  for (PkgEntry &entry : *entries) entry.offset += data_start;
  return true;

}

bool IsSceneBackgroundCandidate(const QString &name) {

  const QString lower = name.toLower();
  if (!lower.endsWith(".tex"_L1)) return false;
  // Masks, effects, particles and fonts are never the background image
  static const QStringList skip = { u"masks/"_s, u"effects/"_s, u"particle"_s, u"fonts/"_s, u"workshop/"_s, u"util/"_s };
  for (const QString &s : skip) {
    if (lower.contains(s)) return false;
  }
  return true;

}

QString SanitizeFileName(QString name) {
  name.replace(QRegularExpression(u"[^A-Za-z0-9._-]"_s), u"_"_s);
  return name;
}

QString SaveImage(const QImage &input, const QString &base_name, QString *error) {

  QImage image = input;
  if (image.width() > kMaxSavedImageSize || image.height() > kMaxSavedImageSize) {
    image = image.scaled(kMaxSavedImageSize, kMaxSavedImageSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
  }
  const bool alpha = image.hasAlphaChannel();
  const QString filename = BackgroundsDir() + u'/' + base_name + (alpha ? u".png"_s : u".jpg"_s);
  if (!image.save(filename, alpha ? "PNG" : "JPG", alpha ? -1 : 92)) {
    if (error) *error = QObject::tr("Couldn't save the image to %1").arg(filename);
    return QString();
  }
  return filename;

}

}  // namespace

QString BackgroundsDir() {

  const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + u"/backgrounds"_s;
  QDir().mkpath(dir);
  return dir;

}

QImage DecodeTex(const QByteArray &data, QString *error) {

  auto fail = [error](const QString &message) {
    if (error) *error = message;
    return QImage();
  };

  Reader r(data);
  TexHeader header;
  if (!ReadTexHeader(&r, &header)) return fail(QObject::tr("Not a Wallpaper Engine texture"));

  const QByteArray container = r.CString(16);
  if (!container.startsWith("TEXB")) return fail(QObject::tr("Unknown texture container"));
  const int version = container.mid(4).toInt();
  const qint32 image_count = r.Int32();
  if (image_count < 1) return fail(QObject::tr("Texture has no images"));
  qint32 free_image_format = -1;
  if (version >= 3) free_image_format = r.Int32();
  if (version >= 4 && r.Int32() != 0) return fail(QObject::tr("Animated (video) textures aren't supported"));

  // First image, biggest mipmap
  const qint32 mipmap_count = r.Int32();
  if (mipmap_count < 1) return fail(QObject::tr("Texture has no mipmaps"));
  const qint32 width = r.Int32();
  const qint32 height = r.Int32();
  bool lz4 = false;
  qint32 decompressed_size = 0;
  if (version >= 2) {
    lz4 = r.Int32() != 0;
    decompressed_size = r.Int32();
  }
  const qint32 byte_count = r.Int32();
  QByteArray bytes = r.Bytes(byte_count);
  if (!r.ok() || width <= 0 || height <= 0) return fail(QObject::tr("Truncated texture"));
  if (lz4) {
    bytes = Lz4Decompress(bytes, decompressed_size);
    if (bytes.isEmpty()) return fail(QObject::tr("Couldn't decompress the texture"));
  }

  QImage image;
  // Textures made from imported pictures contain the original PNG/JPEG file.
  if (free_image_format != -1 || bytes.startsWith("\x89PNG") || bytes.startsWith("\xFF\xD8")) {
    image.loadFromData(bytes);
  }
  else {
    switch (header.format) {
      case 0:  // RGBA8888
        if (bytes.size() >= static_cast<qint64>(width) * height * 4) {
          image = QImage(reinterpret_cast<const uchar*>(bytes.constData()), width, height, width * 4, QImage::Format_RGBA8888).copy();
        }
        break;
      case 4:
        image = DecodeDxt(bytes, width, height, 5);
        break;
      case 6:
        image = DecodeDxt(bytes, width, height, 3);
        break;
      case 7:
        image = DecodeDxt(bytes, width, height, 1);
        break;
      case 9:  // R8
        if (bytes.size() >= static_cast<qint64>(width) * height) {
          image = QImage(reinterpret_cast<const uchar*>(bytes.constData()), width, height, width, QImage::Format_Grayscale8).copy();
        }
        break;
      default:
        return fail(QObject::tr("Unsupported texture format %1").arg(header.format));
    }
  }
  if (image.isNull()) return fail(QObject::tr("Couldn't decode the texture"));

  // The image may only use part of the (power of two) texture.
  if (header.image_width > 0 && header.image_height > 0 && (header.image_width < image.width() || header.image_height < image.height())) {
    image = image.copy(0, 0, std::min(header.image_width, image.width()), std::min(header.image_height, image.height()));
  }

  return image;

}

QImage ExtractSceneImage(const QString &dir, QString *error) {

  struct Candidate {
    QString name;
    QString file;      // Loose .tex file, or the scene.pkg
    qint64 offset = 0;
    qint64 size = 0;
    qint64 area = 0;
  };
  QList<Candidate> candidates;

  auto read_data = [](const Candidate &candidate, const qint64 max_size) {
    QFile file(candidate.file);
    if (!file.open(QIODevice::ReadOnly)) return QByteArray();
    if (candidate.size > 0) {
      file.seek(candidate.offset);
      return file.read(std::min(candidate.size, max_size));
    }
    return file.read(max_size);
  };

  const QString pkg_filename = dir + u"/scene.pkg"_s;
  if (QFile::exists(pkg_filename)) {
    QFile pkg(pkg_filename);
    QList<PkgEntry> entries;
    if (!pkg.open(QIODevice::ReadOnly) || !ReadPkg(&pkg, &entries)) {
      if (error) *error = QObject::tr("Couldn't read scene.pkg");
      return QImage();
    }
    for (const PkgEntry &entry : std::as_const(entries)) {
      if (IsSceneBackgroundCandidate(entry.name)) candidates << Candidate{ entry.name, pkg_filename, entry.offset, entry.size, 0 };
    }
  }
  else {
    QDirIterator it(dir, QStringList() << u"*.tex"_s, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
      const QString filename = it.next();
      const QString name = QDir(dir).relativeFilePath(filename);
      if (IsSceneBackgroundCandidate(name)) candidates << Candidate{ name, filename, 0, 0, 0 };
    }
  }

  // Biggest image first: that's normally the background.
  for (Candidate &candidate : candidates) {
    const QByteArray head = read_data(candidate, 256);
    Reader r(head);
    TexHeader header;
    if (ReadTexHeader(&r, &header)) candidate.area = static_cast<qint64>(header.image_width) * header.image_height;
  }
  std::stable_sort(candidates.begin(), candidates.end(), [](const Candidate &a, const Candidate &b) { return a.area > b.area; });

  QString last_error = QObject::tr("No background texture found in this scene");
  for (const Candidate &candidate : std::as_const(candidates)) {
    if (candidate.area <= 0) continue;
    const QByteArray data = read_data(candidate, 1024LL * 1024 * 1024);
    QString tex_error;
    const QImage image = DecodeTex(data, &tex_error);
    if (!image.isNull()) {
      qLog(Debug) << "Extracted scene background" << candidate.name << image.size();
      return image;
    }
    last_error = candidate.name + u": "_s + tex_error;
    qLog(Warning) << "Couldn't decode" << candidate.name << tex_error;
  }

  if (error) *error = last_error;
  return QImage();

}

bool ReadItem(const QString &dir, Item *item) {

  QFile file(dir + u"/project.json"_s);
  if (!file.open(QIODevice::ReadOnly)) return false;
  QByteArray json = file.readAll();
  if (json.startsWith("\xEF\xBB\xBF")) json.remove(0, 3);
  const QJsonObject project = QJsonDocument::fromJson(json).object();

  item->dir = dir;
  item->id = QFileInfo(dir).fileName();
  item->title = project["title"_L1].toString().trimmed();
  if (item->title.isEmpty()) item->title = item->id;
  item->type = project["type"_L1].toString().toLower();
  item->file = project["file"_L1].toString();

  if (item->type.isEmpty()) {
    const QString lower_file = item->file.toLower();
    if (QFile::exists(dir + u"/scene.pkg"_s) || lower_file.endsWith(".json"_L1)) item->type = u"scene"_s;
    else if (lower_file.endsWith(".mp4"_L1) || lower_file.endsWith(".webm"_L1) || lower_file.endsWith(".mkv"_L1)) item->type = u"video"_s;
    else if (lower_file.endsWith(".html"_L1)) item->type = u"web"_s;
  }

  item->preview.clear();
  const QString preview = project["preview"_L1].toString();
  QStringList previews;
  if (!preview.isEmpty()) previews << preview;
  previews << u"preview.jpg"_s << u"preview.png"_s << u"preview.gif"_s;
  for (const QString &p : std::as_const(previews)) {
    if (QFile::exists(dir + u'/' + p)) {
      item->preview = dir + u'/' + p;
      break;
    }
  }

  return true;

}

QList<Item> FindItems() {

  QStringList libraries;
#ifdef Q_OS_WIN32
  QSettings steam_registry(u"HKEY_CURRENT_USER\\Software\\Valve\\Steam"_s, QSettings::NativeFormat);
  const QString steam_path = steam_registry.value(u"SteamPath"_s).toString();
  if (!steam_path.isEmpty()) libraries << steam_path;
  libraries << u"C:/Program Files (x86)/Steam"_s;
#else
  libraries << QDir::homePath() + u"/.local/share/Steam"_s << QDir::homePath() + u"/.steam/steam"_s;
#endif

  // Other Steam library folders are listed in libraryfolders.vdf
  for (const QString &steam : QStringList(libraries)) {
    QFile vdf(steam + u"/steamapps/libraryfolders.vdf"_s);
    if (!vdf.open(QIODevice::ReadOnly)) continue;
    static const QRegularExpression path_regex(u"\"path\"\\s+\"([^\"]+)\""_s);
    QRegularExpressionMatchIterator it = path_regex.globalMatch(QString::fromUtf8(vdf.readAll()));
    while (it.hasNext()) {
      libraries << it.next().captured(1).replace(u"\\\\"_s, u"/"_s);
    }
  }

  QStringList project_dirs;
  QSet<QString> seen_libraries;
  for (const QString &library : std::as_const(libraries)) {
    const QString canonical = QFileInfo(library).canonicalFilePath().toLower();
    if (canonical.isEmpty() || seen_libraries.contains(canonical)) continue;
    seen_libraries.insert(canonical);
    project_dirs << library + u"/steamapps/workshop/content/"_s + QLatin1String(kAppId)
                 << library + u"/steamapps/common/wallpaper_engine/projects/myprojects"_s
                 << library + u"/steamapps/common/wallpaper_engine/projects/defaultprojects"_s;
  }

  QList<Item> items;
  for (const QString &project_dir : std::as_const(project_dirs)) {
    const QFileInfoList dirs = QDir(project_dir).entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QFileInfo &info : dirs) {
      Item item;
      if (ReadItem(info.absoluteFilePath(), &item)) items << item;
    }
  }

  std::stable_sort(items.begin(), items.end(), [](const Item &a, const Item &b) { return a.title.compare(b.title, Qt::CaseInsensitive) < 0; });
  return items;

}

bool PrepareBackground(const Item &item, Background *background, QString *error) {

  if (item.type == "video"_L1) {
    const QString filename = item.dir + u'/' + item.file;
    if (!QFile::exists(filename)) {
      if (error) *error = QObject::tr("The video file %1 is missing").arg(item.file);
      return false;
    }
    background->video = true;
    background->file = filename;
    return true;
  }

  QImage image;
  QString extract_error;
  if (item.type == "scene"_L1) {
    image = ExtractSceneImage(item.dir, &extract_error);
  }
  // Web wallpapers (and scenes we couldn't read) use the preview picture.
  if (image.isNull() && !item.preview.isEmpty()) {
    QImageReader reader(item.preview);
    reader.setDecideFormatFromContent(true);
    image = reader.read();
  }
  if (image.isNull()) {
    if (error) *error = extract_error.isEmpty() ? QObject::tr("This wallpaper has no usable image") : extract_error;
    return false;
  }

  const QString filename = SaveImage(image, u"wallpaperengine-"_s + SanitizeFileName(item.id), error);
  if (filename.isEmpty()) return false;
  background->video = false;
  background->file = filename;
  return true;

}

QString ImportImage(const QString &filename, QString *error) {

  const QFileInfo info(filename);
  if (info.absolutePath() == QFileInfo(BackgroundsDir()).absoluteFilePath()) return filename;  // Already imported

  QFile file(filename);
  QByteArray data;
  if (file.open(QIODevice::ReadOnly)) data = file.readAll();
  if (data.isEmpty()) {
    QString message = QObject::tr("Couldn't read %1: %2").arg(info.fileName(), file.errorString());
#ifdef Q_OS_WIN32
    // Online-only cloud files (OneDrive "Files On-Demand") can't be read while the sync app isn't running.
    const DWORD attributes = GetFileAttributesW(reinterpret_cast<const wchar_t*>(QDir::toNativeSeparators(filename).utf16()));
    if (attributes != INVALID_FILE_ATTRIBUTES && (attributes & (FILE_ATTRIBUTE_RECALL_ON_DATA_ACCESS | FILE_ATTRIBUTE_RECALL_ON_OPEN | FILE_ATTRIBUTE_OFFLINE))) {
      message = QObject::tr("This file is only stored online (OneDrive). Start OneDrive, or right-click the file in Explorer and choose \"Always keep on this device\", then pick it again.");
    }
#endif
    if (error) *error = message;
    return QString();
  }

  QImage image;
  if (!image.loadFromData(data)) {
    if (error) *error = QObject::tr("%1 isn't a supported image").arg(info.fileName());
    return QString();
  }

  // Keep the original file (format and quality) when it's not huge, otherwise save a scaled down copy.
  const QString hash = QString::fromLatin1(QCryptographicHash::hash(filename.toUtf8(), QCryptographicHash::Sha1).toHex().left(8));
  const QString base_name = u"custom-"_s + SanitizeFileName(info.completeBaseName()).left(40) + u'-' + hash;
  if (image.width() <= kMaxSavedImageSize && image.height() <= kMaxSavedImageSize) {
    QBuffer buffer(&data);
    buffer.open(QIODevice::ReadOnly);
    const QString suffix = QString::fromLatin1(QImageReader::imageFormat(&buffer)).toLower();
    const QString destination = BackgroundsDir() + u'/' + base_name + u'.' + (suffix.isEmpty() ? info.suffix().toLower() : suffix);
    QFile out(destination);
    if (out.open(QIODevice::WriteOnly) && out.write(data) == data.size()) return destination;
  }
  return SaveImage(image, base_name, error);

}

}  // namespace WallpaperEngine
