"""Convert PyTorch checkpoint to TFLite (INT8 quantized) for ESP32-S3 deployment.
Usage:
    python convert.py model_checkpoint.pt
    python convert.py model_checkpoint.pt -o model_int8.tflite --calib calib.npy
"""
import os
os.environ['KMP_DUPLICATE_LIB_OK'] = 'TRUE'

import argparse
import json

import numpy as np
import tensorflow as tf
import torch

from model import TinyHAR, TinyTCN


def build_keras_model(n_channels=6, n_classes=4, seq_len=100, model_type='har'):
    """Build equivalent Keras model for PyTorch TinyHAR/TinyTCN.
    Returns (model, conv_layers, bn_layers, dense_layer) for weight copy.
    """
    inp = tf.keras.Input(shape=(seq_len, n_channels), dtype=tf.float32)
    x = inp

    conv_layers = []
    bn_layers = []

    conv_configs = [
        {'filters': 16, 'kernel_size': 7} if model_type == 'har'
        else {'filters': 16, 'kernel_size': 3, 'dilation_rate': 1},
        {'filters': 32, 'kernel_size': 5} if model_type == 'har'
        else {'filters': 32, 'kernel_size': 3, 'dilation_rate': 2},
        {'filters': 64, 'kernel_size': 3} if model_type == 'har'
        else {'filters': 64, 'kernel_size': 3, 'dilation_rate': 4},
    ]

    for cfg in conv_configs:
        conv = tf.keras.layers.Conv1D(padding='same', activation=None, **cfg)
        bn = tf.keras.layers.BatchNormalization()
        relu = tf.keras.layers.ReLU()
        x = relu(bn(conv(x)))
        conv_layers.append(conv)
        bn_layers.append(bn)

    x = tf.keras.layers.GlobalAveragePooling1D()(x)
    x = tf.keras.layers.Dropout(0.3)(x)
    dense = tf.keras.layers.Dense(n_classes)
    out = dense(x)

    model = tf.keras.Model(inputs=inp, outputs=out, name=model_type)
    return model, conv_layers, bn_layers, dense


def copy_conv1d_weights(pt_layer, tf_layer):
    """Copy PyTorch Conv1d weights to Keras Conv1D."""
    w = pt_layer.weight.detach().cpu().numpy()
    w = np.transpose(w, (2, 1, 0))
    b = pt_layer.bias.detach().cpu().numpy()
    tf_layer.set_weights([w, b])


def copy_bn_weights(pt_layer, tf_layer):
    """Copy PyTorch BatchNorm1d weights to Keras BatchNormalization."""
    gamma = pt_layer.weight.detach().cpu().numpy()
    beta = pt_layer.bias.detach().cpu().numpy()
    mean = pt_layer.running_mean.detach().cpu().numpy()
    var = pt_layer.running_var.detach().cpu().numpy()
    tf_layer.set_weights([gamma, beta, mean, var])


def copy_dense_weights(pt_layer, tf_layer):
    """Copy PyTorch Linear weights to Keras Dense."""
    w = pt_layer.weight.detach().cpu().numpy().T
    b = pt_layer.bias.detach().cpu().numpy()
    tf_layer.set_weights([w, b])


def load_calibration_data(path, n_samples=100):
    """Load calibration data for INT8 quantization."""
    if path is not None:
        data = np.load(path)
        print(f"Loaded calibration data: {data.shape}")
    else:
        print("No calibration data provided, using random data")
        data = np.random.randn(n_samples, 100, 6).astype(np.float32)

    def representative_dataset():
        for i in range(min(n_samples, len(data))):
            yield [data[i:i+1].astype(np.float32)]

    return representative_dataset


def main():
    parser = argparse.ArgumentParser(
        description='Convert PyTorch checkpoint to TFLite')
    parser.add_argument('checkpoint', help='Path to .pt model checkpoint')
    parser.add_argument('-o', '--output', default=None,
                        help='Output .tflite path (default: auto)')
    parser.add_argument('--calib', default=None,
                        help='Calibration data .npy for INT8 quantization')
    parser.add_argument('--no-int8', action='store_true',
                        help='Disable INT8 quantization (keeps float32)')
    parser.add_argument('--model', choices=['har', 'tcn'], default='har',
                        help='Model architecture (default: har)')
    parser.add_argument('--classes', type=int, default=4,
                        help='Number of classes (default: 4)')
    parser.add_argument('--seq-len', type=int, default=100,
                        help='Input sequence length (default: 100)')
    parser.add_argument('--channels', type=int, default=6,
                        help='Input channels (default: 6)')
    args = parser.parse_args()

    do_quantize = not args.no_int8

    # Load PyTorch checkpoint
    print(f"Loading PyTorch checkpoint: {args.checkpoint}")
    checkpoint = torch.load(args.checkpoint, map_location='cpu')
    n_classes = checkpoint.get('n_classes', args.classes)

    if args.model == 'har':
        pt_model = TinyHAR(args.channels, n_classes, args.seq_len)
    else:
        pt_model = TinyTCN(args.channels, n_classes)

    state_dict = checkpoint.get('model_state_dict', checkpoint)
    pt_model.load_state_dict(state_dict)
    pt_model.eval()
    print("PyTorch model loaded")

    # Build Keras model and copy weights
    print("Building Keras model and copying weights...")
    keras_model, tf_convs, tf_bns, tf_dense = build_keras_model(
        args.channels, n_classes, args.seq_len, args.model)

    pt_convs = [pt_model.conv1, pt_model.conv2, pt_model.conv3]
    pt_bns = [pt_model.bn1, pt_model.bn2, pt_model.bn3]

    for pt_conv, tf_conv in zip(pt_convs, tf_convs):
        copy_conv1d_weights(pt_conv, tf_conv)
    for pt_bn, tf_bn in zip(pt_bns, tf_bns):
        copy_bn_weights(pt_bn, tf_bn)

    copy_dense_weights(pt_model.fc, tf_dense)

    # Verify with sample inference
    with torch.no_grad():
        dummy = torch.randn(1, args.seq_len, args.channels)
        pt_out = pt_model(dummy).numpy()
    tf_out = keras_model(dummy.numpy()).numpy()
    diff = np.abs(pt_out - tf_out).max()
    print(f"Weight copy verification — max diff: {diff:.6f}")
    if diff > 0.01:
        print("WARNING: Large weight copy discrepancy!")
    else:
        print("Weight copy OK")

    # Convert to TFLite
    print("Converting to TFLite...")
    converter = tf.lite.TFLiteConverter.from_keras_model(keras_model)

    if do_quantize:
        print("Applying INT8 quantization...")
        converter.optimizations = [tf.lite.Optimize.DEFAULT]
        converter.representative_dataset = load_calibration_data(args.calib)
        converter.target_spec.supported_ops = [
            tf.lite.OpsSet.TFLITE_BUILTINS_INT8]
        converter.inference_input_type = tf.int8
        converter.inference_output_type = tf.int8
    else:
        print("Keeping FP32...")

    tflite_model = converter.convert()

    output_path = args.output
    if output_path is None:
        base = args.checkpoint.replace('.pt', '').replace('.pth', '')
        suffix = '_int8' if do_quantize else '_fp32'
        output_path = f"{base}{suffix}.tflite"

    with open(output_path, 'wb') as f:
        f.write(tflite_model)

    size_kb = len(tflite_model) / 1024
    print(f"TFLite saved to: {output_path} ({size_kb:.1f} KB)")

    # Verify TFLite model
    print("Verifying TFLite model...")
    interpreter = tf.lite.Interpreter(model_content=tflite_model)
    interpreter.allocate_tensors()
    for d in interpreter.get_input_details():
        print(f"  Input:  shape={d['shape']}, dtype={d['dtype']}, "
              f"scale={d['quantization'][0]}, zp={d['quantization'][1]}")
    for d in interpreter.get_output_details():
        print(f"  Output: shape={d['shape']}, dtype={d['dtype']}, "
              f"scale={d['quantization'][0]}, zp={d['quantization'][1]}")

    # Run TFLite inference test
    input_details = interpreter.get_input_details()
    output_details = interpreter.get_output_details()
    test_input = np.random.randn(1, args.seq_len, args.channels).astype(
        np.float32)
    if do_quantize:
        scale, zp = input_details[0]['quantization']
        test_input_quant = (test_input / scale + zp).astype(np.int8)
        interpreter.set_tensor(input_details[0]['index'], test_input_quant)
    else:
        interpreter.set_tensor(input_details[0]['index'], test_input)
    interpreter.invoke()
    tflite_out = interpreter.get_tensor(output_details[0]['index'])
    print(f"  TFLite inference output shape: {tflite_out.shape}")

    # Export as C array for firmware
    c_path = output_path.replace('.tflite', '.h')
    with open(c_path, 'w') as f:
        f.write("#ifndef MODEL_DATA_H\n#define MODEL_DATA_H\n\n")
        f.write(f"const unsigned char model_data[] = {{\n")
        for i, b in enumerate(tflite_model):
            f.write(f"0x{b:02x},")
            if (i + 1) % 12 == 0:
                f.write("\n")
            else:
                f.write(" ")
        f.write(f"\n}};\n")
        f.write(f"const unsigned int model_len = {len(tflite_model)};\n\n")
        f.write("#endif\n")
    print(f"C header exported to: {c_path}")
    print("Done!")


if __name__ == '__main__':
    main()
