import os
os.environ['KMP_DUPLICATE_LIB_OK'] = 'TRUE'

import numpy as np
import pandas as pd
from scipy import stats
from sklearn.model_selection import train_test_split


LABEL_MAP = {
    'idle': 0, 'sit': 0,
    'walk': 1, 'run': 2,
    'wave': 3, 'flick': 3, 'circle': 3,
    'fall': 4,
}


def load_csv(path, label_map=None):
    """Load IMU CSV into DataFrame.
    Expected columns: timestamp,ax,ay,az,gx,gy,gz,label
    """
    if label_map is None:
        label_map = LABEL_MAP
    df = pd.read_csv(path)
    df['label'] = df['label'].map(label_map).fillna(-1).astype(int)
    df = df[df['label'] >= 0].reset_index(drop=True)
    return df


def create_windows(df, window_size=100, stride=50):
    """Sliding window over time series.
    Args:
        df: DataFrame with columns [ax,ay,az,gx,gy,gz,label]
        window_size: frames per window (100 = 2s @ 50Hz)
        stride: frames between windows (50 = 50% overlap)
    Returns:
        X: (n_windows, window_size, 6)
        y: (n_windows,) class labels
    """
    values = df[['ax', 'ay', 'az', 'gx', 'gy', 'gz']].values
    labels = df['label'].values

    X, y = [], []
    for start in range(0, len(values) - window_size + 1, stride):
        end = start + window_size
        X.append(values[start:end])
        label_window = labels[start:end]
        label = stats.mode(label_window, keepdims=True)[0][0]
        y.append(label)

    return np.array(X, dtype=np.float32), np.array(y, dtype=np.int64)


def augment_window(window, noise_std=0.01, scale_range=0.1):
    """Apply augmentations to a single window.
    Returns 3 variants: original + noise + scale.
    """
    variants = [window]

    noise = np.random.normal(0, noise_std, window.shape)
    variants.append(window + noise)

    scale = np.random.uniform(1 - scale_range, 1 + scale_range)
    variants.append(window * scale)

    return variants


def augment_dataset(X, y, repeats=2):
    """Augment entire dataset.
    Args:
        X: (N, T, 6)
        y: (N,)
        repeats: how many augmented copies per sample
    Returns:
        X_aug, y_aug
    """
    X_list, y_list = [X], [y]
    for _ in range(repeats):
        X_aug = np.array([augment_window(w)[0] for w in X])
        X_list.append(X_aug)
        y_list.append(y)
    return np.concatenate(X_list, axis=0), np.concatenate(y_list, axis=0)


def load_and_split(csv_path, window_size=100, stride=50,
                   val_ratio=0.15, test_ratio=0.15, augment=True):
    """Full pipeline: load → window → augment → train/val/test split."""
    df = load_csv(csv_path)
    print(f"Loaded {len(df)} frames, {df['label'].nunique()} classes")

    X, y = create_windows(df, window_size, stride)
    print(f"Created {len(X)} windows, class distribution: {dict(zip(*np.unique(y, return_counts=True)))}")

    if augment:
        X, y = augment_dataset(X, y)
        print(f"After augmentation: {len(X)} windows")

    X_temp, X_test, y_temp, y_test = train_test_split(
        X, y, test_size=test_ratio, stratify=y, random_state=42)
    X_train, X_val, y_train, y_val = train_test_split(
        X_temp, y_temp, test_size=val_ratio / (1 - test_ratio),
        stratify=y_temp, random_state=42)

    print(f"Train: {len(X_train)}, Val: {len(X_val)}, Test: {len(X_test)}")
    return (X_train, y_train), (X_val, y_val), (X_test, y_test)


if __name__ == '__main__':
    import sys
    if len(sys.argv) > 1:
        (X_train, y_train), (X_val, y_val), (X_test, y_test) = load_and_split(sys.argv[1])
        print(f"X_train shape: {X_train.shape}, y_train shape: {y_train.shape}")
    else:
        print("Usage: python dataset.py <path_to_csv>")
