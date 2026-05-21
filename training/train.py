"""Train TinyHAR model on collected IMU data.
Usage:
    python train.py data/imu_data_20260521.csv
    python train.py data/*.csv --epochs 100 --model tcn
"""
import os
os.environ['KMP_DUPLICATE_LIB_OK'] = 'TRUE'

import argparse
import sys

import numpy as np
import torch
import torch.nn as nn
from torch.utils.data import DataLoader, TensorDataset

from dataset import load_and_split
from model import TinyHAR, TinyTCN, count_params


MODEL_REGISTRY = {
    'har': TinyHAR,
    'tcn': TinyTCN,
}


def evaluate(model, X, y, device):
    model.eval()
    with torch.no_grad():
        X = torch.FloatTensor(X).to(device)
        y = torch.LongTensor(y).to(device)
        outputs = model(X)
        _, predicted = torch.max(outputs, 1)
        acc = (predicted == y).float().mean().item()
    return acc


def confusion_matrix(model, X, y, device):
    model.eval()
    with torch.no_grad():
        X = torch.FloatTensor(X).to(device)
        y = torch.LongTensor(y).to(device)
        outputs = model(X)
        _, predicted = torch.max(outputs, 1)
        n_classes = outputs.shape[1]
        cm = np.zeros((n_classes, n_classes), dtype=int)
        for t, p in zip(y.cpu(), predicted.cpu()):
            cm[t.item(), p.item()] += 1
    return cm


def main():
    parser = argparse.ArgumentParser(description='Train TinyHAR on IMU data')
    parser.add_argument('csv_path', help='Path to CSV dataset')
    parser.add_argument('--epochs', type=int, default=50,
                        help='Training epochs (default: 50)')
    parser.add_argument('--batch-size', type=int, default=32,
                        help='Batch size (default: 32)')
    parser.add_argument('--lr', type=float, default=0.001,
                        help='Learning rate (default: 0.001)')
    parser.add_argument('--window', type=int, default=100,
                        help='Window size in frames (default: 100)')
    parser.add_argument('--stride', type=int, default=50,
                        help='Window stride (default: 50)')
    parser.add_argument('--model', choices=MODEL_REGISTRY.keys(), default='har',
                        help='Model architecture (default: har)')
    parser.add_argument('--no-augment', action='store_true',
                        help='Disable data augmentation')
    parser.add_argument('--output', '-o', default=None,
                        help='Output path for ONNX model (default: auto)')
    parser.add_argument('--seed', type=int, default=42,
                        help='Random seed (default: 42)')
    args = parser.parse_args()

    torch.manual_seed(args.seed)
    np.random.seed(args.seed)
    device = torch.device('cuda' if torch.cuda.is_available() else 'cpu')
    print(f"Device: {device}")

    (X_train, y_train), (X_val, y_val), (X_test, y_test) = load_and_split(
        args.csv_path,
        window_size=args.window,
        stride=args.stride,
        augment=not args.no_augment,
    )

    n_classes = len(np.unique(y_train))
    n_channels = X_train.shape[2]
    print(f"Classes: {n_classes}, Channels: {n_channels}, Seq len: {args.window}")

    model_cls = MODEL_REGISTRY[args.model]
    if args.model == 'har':
        model = model_cls(n_channels=n_channels, n_classes=n_classes, seq_len=args.window)
    else:
        model = model_cls(n_channels=n_channels, n_classes=n_classes)

    print(f"Model: {args.model}, Params: {count_params(model):,}")
    model = model.to(device)

    train_dataset = TensorDataset(
        torch.FloatTensor(X_train), torch.LongTensor(y_train))
    train_loader = DataLoader(
        train_dataset, batch_size=args.batch_size, shuffle=True)

    criterion = nn.CrossEntropyLoss()
    optimizer = torch.optim.Adam(model.parameters(), lr=args.lr)
    scheduler = torch.optim.lr_scheduler.ReduceLROnPlateau(
        optimizer, mode='max', factor=0.5, patience=5)

    best_val_acc = 0.0
    for epoch in range(1, args.epochs + 1):
        model.train()
        running_loss = 0.0
        for X_batch, y_batch in train_loader:
            X_batch, y_batch = X_batch.to(device), y_batch.to(device)
            optimizer.zero_grad()
            outputs = model(X_batch)
            loss = criterion(outputs, y_batch)
            loss.backward()
            optimizer.step()
            running_loss += loss.item()

        val_acc = evaluate(model, X_val, y_val, device)
        scheduler.step(val_acc)

        if val_acc > best_val_acc:
            best_val_acc = val_acc

        if epoch % 5 == 0 or epoch == 1:
            print(f"Epoch {epoch:3d}/{args.epochs}  "
                  f"Loss: {running_loss / len(train_loader):.4f}  "
                  f"Val Acc: {val_acc:.3f}  "
                  f"Best: {best_val_acc:.3f}")

    test_acc = evaluate(model, X_test, y_test, device)
    print(f"\n=== Results ===")
    print(f"Best Val Acc: {best_val_acc:.3f}")
    print(f"Test Acc:     {test_acc:.3f}")
    print(f"\nConfusion Matrix (Test):")
    cm = confusion_matrix(model, X_test, y_test, device)
    print(cm)

    base = os.path.splitext(os.path.basename(args.csv_path))[0]
    output_base = args.output if args.output else base

    # Save PyTorch checkpoint (for convert.py → TFLite)
    ckpt_path = f"{output_base}_{args.model}.pt"
    torch.save({
        'model_state_dict': model.state_dict(),
        'n_classes': n_classes,
        'n_channels': n_channels,
        'seq_len': args.window,
        'model_type': args.model,
        'val_acc': best_val_acc,
        'test_acc': test_acc,
    }, ckpt_path)
    print(f"Checkpoint saved to: {ckpt_path}")

    # Export ONNX (for interoperability)
    try:
        onnx_path = f"{output_base}_{args.model}.onnx"
        dummy = torch.randn(1, args.window, n_channels).to(device)
        torch.onnx.export(
            model, dummy, onnx_path,
            input_names=['input'], output_names=['output'],
            dynamic_axes={'input': {0: 'batch', 1: 'window'}},
        )
        print(f"ONNX exported to: {onnx_path}")
    except Exception as e:
        print(f"ONNX export skipped: {e}")


if __name__ == '__main__':
    main()
