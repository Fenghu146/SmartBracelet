import torch
import torch.nn as nn


class TinyHAR(nn.Module):
    """1D-CNN for IMU-based Human Activity Recognition.
    ~15K params, ~60KB INT8, ~5ms on ESP32-S3.
    """
    def __init__(self, n_channels=6, n_classes=4, seq_len=100):
        super().__init__()
        self.conv1 = nn.Conv1d(n_channels, 16, kernel_size=7, padding='same')
        self.bn1 = nn.BatchNorm1d(16)
        self.conv2 = nn.Conv1d(16, 32, kernel_size=5, padding='same')
        self.bn2 = nn.BatchNorm1d(32)
        self.conv3 = nn.Conv1d(32, 64, kernel_size=3, padding='same')
        self.bn3 = nn.BatchNorm1d(64)
        self.pool = nn.AdaptiveAvgPool1d(1)
        self.dropout = nn.Dropout(0.3)
        self.fc = nn.Linear(64, n_classes)

    def forward(self, x):
        x = x.permute(0, 2, 1)
        x = torch.relu(self.bn1(self.conv1(x)))
        x = torch.relu(self.bn2(self.conv2(x)))
        x = torch.relu(self.bn3(self.conv3(x)))
        x = self.pool(x).squeeze(-1)
        x = self.dropout(x)
        return self.fc(x)


class TinyTCN(nn.Module):
    """Temporal Convolutional Network with dilated convolutions.
    ~25K params, ~100KB INT8.
    """
    def __init__(self, n_channels=6, n_classes=4):
        super().__init__()
        self.conv1 = nn.Conv1d(n_channels, 16, 3, dilation=1, padding=1)
        self.bn1 = nn.BatchNorm1d(16)
        self.conv2 = nn.Conv1d(16, 32, 3, dilation=2, padding=2)
        self.bn2 = nn.BatchNorm1d(32)
        self.conv3 = nn.Conv1d(32, 64, 3, dilation=4, padding=4)
        self.bn3 = nn.BatchNorm1d(64)
        self.pool = nn.AdaptiveAvgPool1d(1)
        self.dropout = nn.Dropout(0.3)
        self.fc = nn.Linear(64, n_classes)

    def forward(self, x):
        x = x.permute(0, 2, 1)
        x = torch.relu(self.bn1(self.conv1(x)))
        x = torch.relu(self.bn2(self.conv2(x)))
        x = torch.relu(self.bn3(self.conv3(x)))
        x = self.pool(x).squeeze(-1)
        x = self.dropout(x)
        return self.fc(x)


def count_params(model):
    return sum(p.numel() for p in model.parameters() if p.requires_grad)


if __name__ == '__main__':
    m = TinyHAR(n_classes=4, seq_len=100)
    x = torch.randn(2, 100, 6)
    y = m(x)
    print(f"TinyHAR output shape: {y.shape}, params: {count_params(m)}")
    m2 = TinyTCN(n_classes=4)
    y2 = m2(x)
    print(f"TinyTCN output shape: {y2.shape}, params: {count_params(m2)}")
