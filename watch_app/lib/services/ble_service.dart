import 'dart:async';
import 'dart:typed_data';
import 'package:flutter_blue_plus/flutter_blue_plus.dart';
import '../models/watch_data.dart';

class WatchUuids {
  static const service  = "abcd1000-0000-1000-8000-00805f9b34fb";
  static const steps    = "abcd1001-0000-1000-8000-00805f9b34fb";
  static const battRaw  = "abcd1002-0000-1000-8000-00805f9b34fb";
  static const activity = "abcd1003-0000-1000-8000-00805f9b34fb";
}

enum BleState { disconnected, scanning, connecting, connected }

class BleService {
  BluetoothDevice? _device;
  BluetoothCharacteristic? _stepChar;
  BluetoothCharacteristic? _battChar;
  BluetoothCharacteristic? _actChar;

  final _stateController = StreamController<BleState>.broadcast();
  final _dataController = StreamController<WatchData>.broadcast();

  Stream<BleState> get stateStream => _stateController.stream;
  Stream<WatchData> get dataStream => _dataController.stream;
  BleState get state => _state;

  BleState _state = BleState.disconnected;

  void _setState(BleState s) {
    _state = s;
    _stateController.add(s);
  }

  Stream<ScanResult> scan({Duration timeout = const Duration(seconds: 10)}) {
    _setState(BleState.scanning);
    FlutterBluePlus.startScan(timeout: timeout);
    return FlutterBluePlus.scanResults;
  }

  void stopScan() {
    FlutterBluePlus.stopScan();
    if (_state == BleState.scanning) _setState(BleState.disconnected);
  }

  Future<void> connect(BluetoothDevice device) async {
    _setState(BleState.connecting);
    _device = device;
    await device.connect();
    await device.discoverServices();
    _findChars(device.servicesList);
    _setState(BleState.connected);
    _subscribe();
  }

  void _findChars(List<BluetoothService> services) {
    for (final s in services) {
      if (s.uuid.toString() != WatchUuids.service) continue;
      for (final c in s.characteristics) {
        final u = c.uuid.toString();
        if (u == WatchUuids.steps) _stepChar = c;
        if (u == WatchUuids.battRaw) _battChar = c;
        if (u == WatchUuids.activity) _actChar = c;
      }
    }
  }

  void _subscribe() {
    _stepChar?.setNotifyValue(true);
    _actChar?.setNotifyValue(true);
    _stepChar?.onValueReceived.listen((d) {
      _dataController.add(WatchData.fromBytes(d));
    });
    _actChar?.onValueReceived.listen((d) {
      // activity change — read steps too to get full state
      readAll();
    });
  }

  Future<WatchData> readAll() async {
    final steps = _stepChar != null ? await _stepChar!.read() : Uint8List(0);
    final batt   = _battChar != null ? await _battChar!.read() : Uint8List(0);
    final act    = _actChar != null ? await _actChar!.read() : Uint8List(0);
    final data = WatchData(
      steps: WatchData.fromBytes(steps).steps,
      battRaw: WatchData.parseBattRaw(batt),
      activity: WatchData.parseActivity(act),
    );
    _dataController.add(data);
    return data;
  }

  Future<void> disconnect() async {
    await _stepChar?.setNotifyValue(false);
    await _actChar?.setNotifyValue(false);
    await _device?.disconnect();
    _device = null;
    _stepChar = null;
    _battChar = null;
    _actChar = null;
    _setState(BleState.disconnected);
  }

  void dispose() {
    disconnect();
    _stateController.close();
    _dataController.close();
  }
}
