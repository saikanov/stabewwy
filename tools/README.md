# Tools

## windows/build.bat

Builds Strawberry for Windows (MSVC x64, Release) into `build\` at the repository root. Afterwards `build\strawberry.exe`
can be started directly: the dependency DLLs and plugins are copied next to it.

Requirements (one time):

- Visual Studio 2022 Build Tools with the C++ workload and Windows SDK 10.0.22621
  (`winget install Microsoft.VisualStudio.2022.BuildTools`), CMake (`winget install Kitware.CMake`) and Ninja (`winget install Ninja-build.Ninja`).
- The prebuilt dependencies: `strawberry-msvc-x86_64-release.tar.xz` from
  https://github.com/strawberrymusicplayer/strawberry-msvc-dependencies/releases extracted to `C:\`
  (giving `C:\strawberry_msvc_x86_64_release`), or set `STRAWBERRY_DEPS` to where they are.

Then run `tools\windows\build.bat`. The first build takes a while, later ones only rebuild what changed. It works
while Strawberry is running (the old exe is moved aside), the new version is used from the next start.

## art/

The icons, logo and mascot are drawn as SVG in these pages and rendered to PNG with headless Microsoft Edge.

- `modern-icons.html` / `apply-modern-icons.ps1`: the modern monochrome icon set (`data/icons/modern`, `data/iconsmodern.qrc`).
  The icons are white and get tinted with the theme's text color at runtime.
- `icons.html` / `apply-icons.ps1`: the colorful pastel icon set (`data/icons`), the app logo and Windows `.ico`,
  the cat mascot and the other pictures in `data/pictures`.

Open a page with `#preview` at the end of its URL to see all icons, edit the SVG in it, then run the matching script
(`powershell -File tools\art\apply-modern-icons.ps1`) and rebuild.

The modern icons are inspired by Lucide (https://lucide.dev, ISC license).
