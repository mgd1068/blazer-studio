# HPGL reference fixture

`inkscape-l.svg` is our asymmetric 20 × 10 mm outline. `inkscape-l.hpgl` is the
actual output of the installed Inkscape HPGL exporter, not a hand-written
approximation. Generated with Inkscape 1.4.3's extension on Ubuntu 26.04:

```sh
/usr/bin/python3 /usr/share/inkscape/extensions/hpgl_output.py \
  --precut=false --toolOffset=0 --overcut=0 \
  --output=tests/fixtures/hpgl/inkscape-l.hpgl tests/fixtures/hpgl/inkscape-l.svg
```

Default resolution is 1016 dpi (40 plotter units/mm); knife compensation and
overcut are disabled for a geometry comparison. This fixture is our generated
geometry; no extension source code is bundled or used in the application.
