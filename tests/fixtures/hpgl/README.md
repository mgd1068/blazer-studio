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

## Scaling references

The three `sc-*.hpgl` files are our 20×10-mm frame with an inner asymmetric
10×10-mm polyline. They exercise centred isotropic scaling and reversed P1/P2
with 0%/100% margins. `scaling-expectations.json` contains coordinates obtained
independently through Inkscape 1.4.3's HPGL2 input extension:

```sh
/usr/bin/python3 /usr/share/inkscape/extensions/hpgl2_input.py \
  --width=1 --height=1 --resolution=1016 --break-apart=true \
  --output=reference.svg tests/fixtures/hpgl/sc-reversed-left.hpgl
```

The baked SVG has a 25.4×25.4 viewBox in millimetres and no transforms. Its M/L
points are converted to our Y-up coordinates by `y = 25.4 - svgY` and rounded to
12 decimal places. No alignment, scaling, or fitting to the expected output is
performed. In particular, reversed P1/P2 reverses the placement of unused margins;
the nominal 0% left case occupies x=10…20 mm, and 100% occupies x=0…10 mm.
The outer frame fixes the common origin so normalizing the imported drawing
cannot hide an incorrect margin offset. The application uses its own parser;
the reference tool is only used to produce comparison data.
