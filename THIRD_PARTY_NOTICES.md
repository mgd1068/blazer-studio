# Third-party notices

The custom free-redistribution license applies to original Blazer Studio code and
original documentation. It does not relicense third-party components.

- **Qt 6 Core, Gui, Quick, QML, Quick Controls, SVG, Image Formats and Serial Port:** copyright
  The Qt Company and their respective contributors. The Linux application links
  dynamically to system Qt libraries. LGPLv3 terms and relevant GPLv3 text are
  included in `LICENSES/`. Their commercial permissions and library modification,
  replacement and debugging rights are not restricted by our application license.
  Qt sources and exact distro patches must be made available as required if a
  distributor bundles these libraries; this repository supplies application
  sources, not a bundled Qt distribution. See <https://www.qt.io/licensing/> and
  <https://doc.qt.io/qt-6/lgpl.html>.
- **DejaVu Sans:** used through the installed system font. Font binaries are not
  bundled; their licenses are supplied by the system font package.
- **BlazeX manufacturer reference:** numerical processing parameters and original
  labels are attributed to <https://de.blazexlaser.com/pages/maschineneinstellung>.
  Retrieval date and source digest are in `profiles/manufacturer-materials.json`.
  Manufacturer material is excluded from claims of original authorship. No
  endorsement, model equivalence or completed material test is implied.
- **Beautiful Soup 4:** optional system Python dependency of the table extraction
  tool, MIT license; its code is not vendored here.
- **GRBL / Grbl-ESP32 protocol documentation:** consulted to independently implement
  the host interface. Firmware source code is not copied or bundled; controller
  firmware remains subject to its original licenses.

For a source distribution preserve this file and `LICENSE`. A binary distributor
must additionally satisfy the license obligations of whatever dependencies it
actually includes. User-created designs retain their own rights.

- **QR-Code-generator (C++), Project Nayuki:** dynamically linked system library
  `libqrcodegencpp`, MIT/Expat license. Complete copyright/license notice in
  `LICENSES/QR-Code-generator.txt`. Its own permissions are not restricted by the
  application license. Source: <https://github.com/nayuki/QR-Code-generator>.
- **Depth Anything V2 Small:** optional local depth estimation, Apache-2.0;
  independently exported ONNX weights from
  <https://huggingface.co/onnx-community/depth-anything-v2-small>, based on
  <https://github.com/DepthAnything/Depth-Anything-V2>. Model revision and SHA-256
  digests are pinned in `tools/setup_depth.py`. Weights are downloaded separately
  into the user's cache, not bundled in this repository. Apache-2.0 text is in
  `LICENSES/Apache-2.0.txt`. The optional Python environment supplies ONNX Runtime,
  NumPy and Pillow under their respective licenses, not our application license.
- **Inkscape:** optional separately installed conversion program for PDF/AI/EPS,
  invoked as a process. No Inkscape source/binary is bundled. Its own GPL rights
  remain unchanged. Source: <https://inkscape.org/>.
