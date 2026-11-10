#!/usr/bin/env python3
"""Local relative depth inference. No uploads, downloads, or hardware commands."""
import argparse
from pathlib import Path
import numpy as np
from PIL import Image, ImageOps
import onnxruntime as ort

ort.disable_telemetry_events()


def estimate(source, output, model):
    image = Image.open(source)
    if image.width * image.height > 16_000_000:
        raise ValueError('Image exceeds 16 megapixels')
    image = ImageOps.exif_transpose(image).convert('RGB')
    # Depth Anything V2 DPT preprocessing: lower-bound resize, multiples of 14.
    scale = max(518 / image.width, 518 / image.height)
    width = max(14, round(image.width * scale / 14) * 14)
    height = max(14, round(image.height * scale / 14) * 14)
    if width * height > 2_000_000:
        raise ValueError('Image aspect ratio too extreme')
    array = np.asarray(image.resize((width, height), Image.Resampling.BICUBIC), dtype=np.float32) / 255
    array = (array - [0.485, 0.456, 0.406]) / [0.229, 0.224, 0.225]
    tensor = array.transpose(2, 0, 1)[None].astype(np.float32)
    options = ort.SessionOptions()
    options.intra_op_num_threads = 4
    session = ort.InferenceSession(str(model), sess_options=options, providers=['CPUExecutionProvider'])
    result = session.run(None, {session.get_inputs()[0].name: tensor})[0].squeeze()
    if result.ndim != 2 or not np.isfinite(result).all():
        raise ValueError('Invalid model output')
    low, high = np.percentile(result, [1, 99])
    if high <= low:
        raise ValueError('No depth variation found')
    # Model predicts relative inverse depth: nearby surfaces become white (less removal).
    result = np.clip((result - low) / (high - low), 0, 1)
    depth = Image.fromarray((result * 65535).astype(np.uint16))
    target = image.size
    depth = depth.resize(target, Image.Resampling.BILINEAR)
    depth.thumbnail((512, 512), Image.Resampling.BILINEAR)
    depth.save(output)


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('source', type=Path)
    parser.add_argument('output', type=Path)
    parser.add_argument('--model', required=True, type=Path)
    args = parser.parse_args()
    estimate(args.source, args.output, args.model)
