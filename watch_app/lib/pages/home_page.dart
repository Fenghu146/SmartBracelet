import 'dart:async';
import 'package:flutter/material.dart';
import 'package:flutter_blue_plus/flutter_blue_plus.dart';
import '../services/ble_service.dart';
import '../models/watch_data.dart';

class HomePage extends StatefulWidget {
  const HomePage({super.key});
  @override
  State<HomePage> createState() => _HomePageState();
}

class _HomePageState extends State<HomePage> {
  final _ble = BleService();
  List<ScanResult> _devices = [];
  WatchData _data = WatchData();
  StreamSubscription? _stateSub;
  StreamSubscription? _dataSub;

  @override
  void initState() {
    super.initState();
    _stateSub = _ble.stateStream.listen(_onStateChange);
    _dataSub = _ble.dataStream.listen(_onData);
  }

  @override
  void dispose() {
    _stateSub?.cancel();
    _dataSub?.cancel();
    _ble.dispose();
    super.dispose();
  }

  void _onStateChange(BleState s) => setState(() {});
  void _onData(WatchData d) => setState(() => _data = d);

  void _scan() {
    setState(() => _devices = []);
    _ble.scan().listen((r) {
      if (!mounted) return;
      if (r.device.name == "SmartBracelet" &&
          !_devices.any((d) => d.device.id == r.device.id)) {
        setState(() => _devices.add(r));
      }
    }, onDone: () => setState(() {}));
  }

  Future<void> _connect(BluetoothDevice d) async {
    _ble.stopScan();
    await _ble.connect(d);
    await _ble.readAll();
  }

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      backgroundColor: const Color(0xFF0d0d1a),
      appBar: AppBar(
        backgroundColor: const Color(0xFF1a1a2e),
        title: const Text('SmartBracelet'),
        actions: [
          if (_ble.state == BleState.connected)
            IconButton(
              icon: const Icon(Icons.refresh),
              onPressed: () => _ble.readAll(),
            ),
        ],
      ),
      body: _buildBody(),
    );
  }

  Widget _buildBody() {
    switch (_ble.state) {
      case BleState.disconnected:
        return _buildScanView();
      case BleState.scanning:
        return const Center(child: CircularProgressIndicator());
      case BleState.connecting:
        return const Center(child: CircularProgressIndicator());
      case BleState.connected:
        return _buildDataView();
    }
  }

  Widget _buildScanView() {
    return Column(
      children: [
        const SizedBox(height: 24),
        ElevatedButton.icon(
          onPressed: _scan,
          icon: const Icon(Icons.bluetooth_searching),
          label: const Text('Scan'),
          style: ElevatedButton.styleFrom(
            backgroundColor: const Color(0xFF00d4ff),
            foregroundColor: Colors.black,
            padding: const EdgeInsets.symmetric(horizontal: 32, vertical: 14),
          ),
        ),
        const SizedBox(height: 16),
        if (_devices.isEmpty)
          const Padding(
            padding: EdgeInsets.only(top: 40),
            child: Text('No devices found',
                style: TextStyle(color: Color(0xFF555566))),
          )
        else
          Expanded(
            child: ListView.builder(
              itemCount: _devices.length,
              itemBuilder: (_, i) {
                final d = _devices[i].device;
                return Card(
                  color: const Color(0xFF1a1a2e),
                  child: ListTile(
                    title: Text(d.name, style: const TextStyle(color: Colors.white)),
                    subtitle: Text(d.id.toString(),
                        style: const TextStyle(color: Color(0xFF888899))),
                    trailing: ElevatedButton(
                      onPressed: () => _connect(d),
                      child: const Text('Connect'),
                    ),
                  ),
                );
              },
            ),
          ),
      ],
    );
  }

  Widget _buildDataView() {
    return Padding(
      padding: const EdgeInsets.all(20),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          Row(
            children: [
              Container(
                width: 12, height: 12,
                decoration: const BoxDecoration(
                  shape: BoxShape.circle, color: Color(0xFF00d488),
                ),
              ),
              const SizedBox(width: 8),
              const Text('Connected',
                  style: TextStyle(color: Color(0xFF888899), fontSize: 13)),
            ],
          ),
          const SizedBox(height: 28),

          // Steps
          _dataTile(
            'Steps',
            '${_data.steps}',
            Icons.directions_walk,
            _data.steps > 0 ? _data.steps / 200.0 : 0,
          ),
          const SizedBox(height: 20),

          // Battery
          _dataTile(
            'Battery',
            _data.isUsbPowered ? 'USB' : '${_data.battRaw} mV',
            Icons.battery_std,
            _data.isUsbPowered ? 1.0 : (_data.battRaw / 4200.0).clamp(0, 1),
            color: _data.isUsbPowered ? const Color(0xFF00d488) : null,
          ),
          const SizedBox(height: 20),

          // Activity
          Row(
            children: [
              const Icon(Icons.fitness_center, color: Color(0xFF00d4ff)),
              const SizedBox(width: 12),
              Column(
                crossAxisAlignment: CrossAxisAlignment.start,
                children: [
                  const Text('Activity',
                      style: TextStyle(color: Color(0xFF888899), fontSize: 12)),
                  const SizedBox(height: 4),
                  Text(_data.activityName,
                      style: const TextStyle(
                          color: Colors.white,
                          fontSize: 22,
                          fontWeight: FontWeight.bold)),
                ],
              ),
            ],
          ),
          const SizedBox(height: 24),

          // Debug hex dump
          Container(
            padding: const EdgeInsets.all(12),
            decoration: BoxDecoration(
              color: const Color(0xFF1a1a2e),
              borderRadius: BorderRadius.circular(8),
            ),
            child: Column(
              crossAxisAlignment: CrossAxisAlignment.start,
              children: [
                const Text('--- Debug ---',
                    style: TextStyle(color: Color(0xFF555566), fontSize: 11)),
                const SizedBox(height: 4),
                Text('steps:   0x ${_data.steps.toRadixString(16).padLeft(8, '0')}',
                    style: const TextStyle(color: Color(0xFF888899), fontSize: 12)),
                Text('battRaw: 0x ${_data.battRaw.toRadixString(16).padLeft(4, '0')}',
                    style: const TextStyle(color: Color(0xFF888899), fontSize: 12)),
                Text('activity: ${_data.activity}',
                    style: const TextStyle(color: Color(0xFF888899), fontSize: 12)),
              ],
            ),
          ),
          const Spacer(),

          // Disconnect button
          Center(
            child: TextButton.icon(
              onPressed: () => _ble.disconnect(),
              icon: const Icon(Icons.link_off, color: Color(0xFF555566)),
              label: const Text('Disconnect',
                  style: TextStyle(color: Color(0xFF555566))),
            ),
          ),
        ],
      ),
    );
  }

  Widget _dataTile(String label, String value, IconData icon, double progress,
      {Color? color}) {
    return Row(
      children: [
        Icon(icon, color: color ?? const Color(0xFF00d4ff)),
        const SizedBox(width: 12),
        Expanded(
          child: Column(
            crossAxisAlignment: CrossAxisAlignment.start,
            children: [
              Text(label,
                  style: const TextStyle(
                      color: Color(0xFF888899), fontSize: 12)),
              const SizedBox(height: 4),
              Text(value,
                  style: TextStyle(
                      color: color ?? Colors.white,
                      fontSize: 22,
                      fontWeight: FontWeight.bold)),
              const SizedBox(height: 6),
              ClipRRect(
                borderRadius: BorderRadius.circular(2),
                child: LinearProgressIndicator(
                  value: progress,
                  backgroundColor: const Color(0xFF333344),
                  valueColor:
                      AlwaysStoppedAnimation(color ?? const Color(0xFF00d4ff)),
                  minHeight: 4,
                ),
              ),
            ],
          ),
        ),
      ],
    );
  }
}
