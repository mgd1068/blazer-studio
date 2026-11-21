"""Independent image acceptance using the installed ZBar decoder, never our encoder.

Input is Qt-rendered grayscale PGM and the expected bytes as hex. No Python image
packages are required. Exit 77 explicitly identifies an unavailable decoder.
"""
import ctypes as c
import ctypes.util
import sys

library = ctypes.util.find_library("zbar")
if not library:
    print("Independent ZBar decoder unavailable")
    sys.exit(77)
z = c.CDLL(library)
for name, result, args in [
    ("zbar_image_scanner_create", c.c_void_p, []),
    ("zbar_image_scanner_destroy", None, [c.c_void_p]),
    ("zbar_image_scanner_set_config", c.c_int, [c.c_void_p, c.c_int, c.c_int, c.c_int]),
    ("zbar_image_create", c.c_void_p, []),
    ("zbar_image_destroy", None, [c.c_void_p]),
    ("zbar_image_set_format", None, [c.c_void_p, c.c_ulong]),
    ("zbar_image_set_size", None, [c.c_void_p, c.c_uint, c.c_uint]),
    ("zbar_image_set_data", None, [c.c_void_p, c.c_void_p, c.c_ulong, c.c_void_p]),
    ("zbar_scan_image", c.c_int, [c.c_void_p, c.c_void_p]),
    ("zbar_image_first_symbol", c.c_void_p, [c.c_void_p]),
    ("zbar_symbol_next", c.c_void_p, [c.c_void_p]),
    ("zbar_symbol_get_type", c.c_int, [c.c_void_p]),
    ("zbar_symbol_get_data", c.c_void_p, [c.c_void_p]),
    ("zbar_symbol_get_data_length", c.c_uint, [c.c_void_p]),
]:
    function = getattr(z, name)
    function.restype, function.argtypes = result, args
with open(sys.argv[1], "rb") as f:
    assert f.readline() == b"P5\n"
    width, height = map(int, f.readline().split())
    assert f.readline() == b"255\n"
    data = f.read()
assert len(data) == width * height
buffer = c.create_string_buffer(data)
scanner, image = z.zbar_image_scanner_create(), z.zbar_image_create()
try:
    z.zbar_image_scanner_set_config(scanner, 0, 0, 0)
    z.zbar_image_scanner_set_config(scanner, 128, 0, 1)  # Only Code 128.
    z.zbar_image_set_format(image, int.from_bytes(b"Y800", "little"))
    z.zbar_image_set_size(image, width, height)
    z.zbar_image_set_data(image, buffer, len(data), None)
    assert z.zbar_scan_image(scanner, image) >= 0
    decoded = []
    symbol = z.zbar_image_first_symbol(image)
    while symbol:
        assert z.zbar_symbol_get_type(symbol) == 128
        decoded.append(c.string_at(z.zbar_symbol_get_data(symbol), z.zbar_symbol_get_data_length(symbol)))
        symbol = z.zbar_symbol_next(symbol)
    expected = [] if sys.argv[2] == "-" else [bytes.fromhex(sys.argv[2])]
    assert decoded == expected, (decoded, expected)
finally:
    z.zbar_image_destroy(image)
    z.zbar_image_scanner_destroy(scanner)
