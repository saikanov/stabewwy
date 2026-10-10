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

#ifndef WALLPAPERENGINE_H
#define WALLPAPERENGINE_H

#include "config.h"

#include <QList>
#include <QString>
#include <QImage>

// Support for using Wallpaper Engine (Steam app 431960) wallpapers as background:
// finds the installed wallpapers and turns them into something Strawberry can show.
namespace WallpaperEngine {

struct Item {
  QString id;       // Workshop id or project folder name
  QString dir;      // Project folder
  QString title;
  QString type;     // "video", "scene", "web" (lower case), or empty if unknown
  QString file;     // Main file from project.json (video file, scene.json, index.html)
  QString preview;  // Absolute path of the preview image, may be empty
};

// All wallpapers in the Steam workshop folders and Wallpaper Engine's own project folders, in every Steam library.
QList<Item> FindItems();

// Reads one project folder (containing project.json). Returns false if it isn't a wallpaper.
bool ReadItem(const QString &dir, Item *item);

struct Background {
  bool video = false;  // Play file as a looping video, otherwise it's an image
  QString file;
};

// Prepares a wallpaper to be used as background: video wallpapers are played directly, the main image of scene
// wallpapers is extracted (static, the scene's effects aren't rendered), and other types use their preview image.
// Extracted images are saved in Strawberry's data folder. Can take a few seconds for big scenes.
bool PrepareBackground(const Item &item, Background *background, QString *error);

// Extracts the biggest texture of a scene wallpaper (normally its background image) from scene.pkg or the materials folder.
QImage ExtractSceneImage(const QString &dir, QString *error = nullptr);

// Decodes a Wallpaper Engine .tex texture.
QImage DecodeTex(const QByteArray &data, QString *error = nullptr);

// Folder where backgrounds picked in "Customize appearance" are copied/extracted to.
QString BackgroundsDir();

// Copies a picked image into BackgroundsDir(), so the background keeps working when the original file isn't available,
// e.g. an online-only OneDrive file. Returns the new path, or an empty string and an explanation in error.
QString ImportImage(const QString &filename, QString *error);

}  // namespace WallpaperEngine

#endif  // WALLPAPERENGINE_H
