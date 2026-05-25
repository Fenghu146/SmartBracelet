import 'dart:typed_data';

class WatchData {
  final int steps;
  final int battRaw;
  final int activity;

  WatchData({this.steps = 0, this.battRaw = 0, this.activity = 2});

  bool get isUsbPowered => battRaw == 0xFFFF;
  String get activityName => activity < 3 ? _names[activity] : '?';
  static const _names = ['Walk', 'Run', 'Idle'];

  static WatchData fromBytes(Uint8List bytes) {
    return WatchData(
      steps: ByteData.view(bytes.buffer).getUint32(0, Endian.little),
    );
  }

  static int parseActivity(Uint8List bytes) => bytes.isNotEmpty ? bytes[0] : 2;

  static int parseBattRaw(Uint8List bytes) =>
      bytes.length >= 2
          ? ByteData.view(bytes.buffer).getUint16(0, Endian.little)
          : 0;
}
