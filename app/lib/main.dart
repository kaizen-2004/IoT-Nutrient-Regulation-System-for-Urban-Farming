import 'dart:async';
import 'dart:convert';
import 'dart:io';

import 'package:flutter/material.dart';
import 'package:http/http.dart' as http;
import 'package:mobile_scanner/mobile_scanner.dart';
import 'package:shared_preferences/shared_preferences.dart';

void main() {
  runApp(const NutrientRegulationApp());
}

class NutrientRegulationApp extends StatefulWidget {
  const NutrientRegulationApp({super.key});

  @override
  State<NutrientRegulationApp> createState() => _NutrientRegulationAppState();
}

class _NutrientRegulationAppState extends State<NutrientRegulationApp> {
  DeviceRecord? _device;
  bool _loading = true;

  @override
  void initState() {
    super.initState();
    _loadDevice();
  }

  Future<void> _loadDevice() async {
    final device = await DeviceStore.load();
    if (!mounted) {
      return;
    }

    setState(() {
      _device = device;
      _loading = false;
    });
  }

  Future<void> _saveDevice(DeviceRecord device) async {
    await DeviceStore.save(device);
    if (!mounted) {
      return;
    }

    setState(() {
      _device = device;
    });
  }

  Future<void> _clearDevice() async {
    await DeviceStore.clear();
    if (!mounted) {
      return;
    }

    setState(() {
      _device = null;
    });
  }

  @override
  Widget build(BuildContext context) {
    return MaterialApp(
      title: 'Nutrient Regulation',
      debugShowCheckedModeBanner: false,
      theme: ThemeData(
        colorScheme: ColorScheme.fromSeed(
          seedColor: const Color(0xFF2B7A4B),
          brightness: Brightness.light,
        ),
        scaffoldBackgroundColor: const Color(0xFFF4F7F1),
        useMaterial3: true,
      ),
      home: _loading
          ? const SplashScreen()
          : (_device == null
                ? OnboardingScreen(onDeviceProvisioned: _saveDevice)
                : DashboardScreen(
                    device: _device!,
                    onDeviceUpdated: _saveDevice,
                    onForgetDevice: _clearDevice,
                  )),
    );
  }
}

class SplashScreen extends StatelessWidget {
  const SplashScreen({super.key});

  @override
  Widget build(BuildContext context) {
    return const Scaffold(body: Center(child: CircularProgressIndicator()));
  }
}

class DeviceStore {
  static const _deviceKey = 'device_record_v1';

  static Future<DeviceRecord?> load() async {
    final prefs = await SharedPreferences.getInstance();
    final raw = prefs.getString(_deviceKey);
    if (raw == null || raw.isEmpty) {
      return null;
    }

    try {
      return DeviceRecord.fromJson(jsonDecode(raw) as Map<String, dynamic>);
    } catch (_) {
      return null;
    }
  }

  static Future<void> save(DeviceRecord device) async {
    final prefs = await SharedPreferences.getInstance();
    await prefs.setString(_deviceKey, jsonEncode(device.toJson()));
  }

  static Future<void> clear() async {
    final prefs = await SharedPreferences.getInstance();
    await prefs.remove(_deviceKey);
  }
}

class DeviceRecord {
  const DeviceRecord({
    required this.deviceId,
    required this.deviceName,
    required this.model,
    required this.setupAp,
    required this.setupIp,
    required this.lastKnownIp,
  });

  final String deviceId;
  final String deviceName;
  final String model;
  final String setupAp;
  final String setupIp;
  final String lastKnownIp;

  Map<String, dynamic> toJson() => {
    'deviceId': deviceId,
    'deviceName': deviceName,
    'model': model,
    'setupAp': setupAp,
    'setupIp': setupIp,
    'lastKnownIp': lastKnownIp,
  };

  factory DeviceRecord.fromJson(Map<String, dynamic> json) {
    return DeviceRecord(
      deviceId: (json['deviceId'] ?? '') as String,
      deviceName: (json['deviceName'] ?? 'Vertical Farm Controller') as String,
      model: (json['model'] ?? 'NRS-C3') as String,
      setupAp: (json['setupAp'] ?? 'NutrientReg-Setup') as String,
      setupIp: (json['setupIp'] ?? '192.168.4.1') as String,
      lastKnownIp: (json['lastKnownIp'] ?? '') as String,
    );
  }

  DeviceRecord copyWith({
    String? deviceId,
    String? deviceName,
    String? model,
    String? setupAp,
    String? setupIp,
    String? lastKnownIp,
  }) {
    return DeviceRecord(
      deviceId: deviceId ?? this.deviceId,
      deviceName: deviceName ?? this.deviceName,
      model: model ?? this.model,
      setupAp: setupAp ?? this.setupAp,
      setupIp: setupIp ?? this.setupIp,
      lastKnownIp: lastKnownIp ?? this.lastKnownIp,
    );
  }
}

class QrPayload {
  const QrPayload({
    required this.version,
    required this.model,
    required this.deviceId,
    required this.setupAp,
    required this.setupIp,
  });

  final int version;
  final String model;
  final String deviceId;
  final String setupAp;
  final String setupIp;

  factory QrPayload.fromRaw(String raw) {
    final decoded = jsonDecode(raw) as Map<String, dynamic>;
    return QrPayload(
      version: (decoded['v'] ?? 1) as int,
      model: (decoded['model'] ?? 'NRS-C3') as String,
      deviceId: (decoded['deviceId'] ?? '') as String,
      setupAp: (decoded['setupAp'] ?? 'NutrientReg-Setup') as String,
      setupIp: (decoded['setupIp'] ?? '192.168.4.1') as String,
    );
  }
}

class ApiException implements Exception {
  const ApiException(this.message);

  final String message;

  @override
  String toString() => message;
}

class DeviceApi {
  const DeviceApi();

  Uri _uri(String host, String path) {
    return Uri.parse('http://$host$path');
  }

  Future<Map<String, dynamic>> _getJson(String host, String path) async {
    final response = await http
        .get(_uri(host, path), headers: {'Accept': 'application/json'})
        .timeout(const Duration(seconds: 8));
    return _decodeJson(response);
  }

  Future<Map<String, dynamic>> _postJson(
    String host,
    String path,
    Map<String, dynamic> body,
  ) async {
    final response = await http
        .post(
          _uri(host, path),
          headers: {
            'Accept': 'application/json',
            'Content-Type': 'application/json',
          },
          body: jsonEncode(body),
        )
        .timeout(const Duration(seconds: 12));
    return _decodeJson(response);
  }

  Map<String, dynamic> _decodeJson(http.Response response) {
    Map<String, dynamic> body = <String, dynamic>{};
    if (response.body.isNotEmpty) {
      final decoded = jsonDecode(response.body);
      if (decoded is Map<String, dynamic>) {
        body = decoded;
      }
    }

    if (response.statusCode < 200 || response.statusCode >= 300) {
      throw ApiException(
        (body['error'] ?? body['message'] ?? 'Request failed') as String,
      );
    }
    return body;
  }

  Future<Map<String, dynamic>> fetchProvisioningInfo(String host) {
    return _getJson(host, '/api/provisioning/info');
  }

  Future<Map<String, dynamic>> configureProvisioning({
    required String host,
    required String ssid,
    required String password,
  }) {
    return _postJson(host, '/api/provisioning/configure', {
      'ssid': ssid,
      'password': password,
    });
  }

  Future<Map<String, dynamic>> fetchProvisioningResult(String host) {
    return _getJson(host, '/api/provisioning/result');
  }

  Future<Map<String, dynamic>> fetchInfo(String host) {
    return _getJson(host, '/api/info');
  }

  Future<Map<String, dynamic>> fetchStatus(String host) {
    return _getJson(host, '/api/status');
  }

  Future<Map<String, dynamic>> resetWifi(String host) {
    return _postJson(host, '/api/device/reset-wifi', {'confirm': true});
  }
}

class OnboardingScreen extends StatefulWidget {
  const OnboardingScreen({super.key, required this.onDeviceProvisioned});

  final Future<void> Function(DeviceRecord device) onDeviceProvisioned;

  @override
  State<OnboardingScreen> createState() => _OnboardingScreenState();
}

class _OnboardingScreenState extends State<OnboardingScreen> {
  QrPayload? _payload;
  String? _error;

  Future<void> _openScanner() async {
    final raw = await Navigator.of(
      context,
    ).push<String>(MaterialPageRoute(builder: (_) => const QrScannerScreen()));

    if (raw == null) {
      return;
    }

    _applyPayload(raw);
  }

  Future<void> _pastePayload() async {
    final controller = TextEditingController();
    final raw = await showDialog<String>(
      context: context,
      builder: (context) {
        return AlertDialog(
          title: const Text('Paste QR payload'),
          content: TextField(
            controller: controller,
            maxLines: 6,
            decoration: const InputDecoration(
              hintText: '{"v":1,"model":"NRS-C3",...}',
            ),
          ),
          actions: [
            TextButton(
              onPressed: () => Navigator.of(context).pop(),
              child: const Text('Cancel'),
            ),
            FilledButton(
              onPressed: () =>
                  Navigator.of(context).pop(controller.text.trim()),
              child: const Text('Use payload'),
            ),
          ],
        );
      },
    );

    if (raw == null || raw.isEmpty) {
      return;
    }

    _applyPayload(raw);
  }

  void _applyPayload(String raw) {
    try {
      final payload = QrPayload.fromRaw(raw);
      setState(() {
        _payload = payload;
        _error = null;
      });
    } catch (_) {
      setState(() {
        _error =
            'The scanned code does not match the expected device QR format.';
      });
    }
  }

  Future<void> _continueProvisioning() async {
    final payload = _payload;
    if (payload == null) {
      return;
    }

    final device = await Navigator.of(context).push<DeviceRecord>(
      MaterialPageRoute(builder: (_) => ProvisioningScreen(payload: payload)),
    );

    if (device == null) {
      return;
    }

    await widget.onDeviceProvisioned(device);
  }

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      appBar: AppBar(title: const Text('Connect Device')),
      body: ListView(
        padding: const EdgeInsets.all(20),
        children: [
          _HeroCard(
            title: 'V380-style onboarding',
            subtitle:
                'Scan the printed QR sticker, connect your phone to the setup AP, and send Wi-Fi credentials to the controller.',
            icon: Icons.qr_code_2,
          ),
          const SizedBox(height: 16),
          Card(
            child: Padding(
              padding: const EdgeInsets.all(16),
              child: Column(
                crossAxisAlignment: CrossAxisAlignment.start,
                children: [
                  Text(
                    'Start setup',
                    style: Theme.of(context).textTheme.titleMedium,
                  ),
                  const SizedBox(height: 12),
                  FilledButton.icon(
                    onPressed: _openScanner,
                    icon: const Icon(Icons.qr_code_scanner),
                    label: const Text('Scan device QR'),
                  ),
                  const SizedBox(height: 8),
                  TextButton.icon(
                    onPressed: _pastePayload,
                    icon: const Icon(Icons.content_paste),
                    label: const Text('Paste QR payload manually'),
                  ),
                  if (_error != null) ...[
                    const SizedBox(height: 8),
                    Text(
                      _error!,
                      style: TextStyle(
                        color: Theme.of(context).colorScheme.error,
                      ),
                    ),
                  ],
                ],
              ),
            ),
          ),
          const SizedBox(height: 16),
          if (_payload != null)
            Card(
              child: Padding(
                padding: const EdgeInsets.all(16),
                child: Column(
                  crossAxisAlignment: CrossAxisAlignment.start,
                  children: [
                    Text(
                      'Detected device',
                      style: Theme.of(context).textTheme.titleMedium,
                    ),
                    const SizedBox(height: 12),
                    _DetailRow(label: 'Model', value: _payload!.model),
                    _DetailRow(label: 'Device ID', value: _payload!.deviceId),
                    _DetailRow(label: 'Setup AP', value: _payload!.setupAp),
                    _DetailRow(label: 'Setup IP', value: _payload!.setupIp),
                    const SizedBox(height: 16),
                    FilledButton(
                      onPressed: _continueProvisioning,
                      child: const Text('Continue to provisioning'),
                    ),
                  ],
                ),
              ),
            ),
        ],
      ),
    );
  }
}

class ProvisioningScreen extends StatefulWidget {
  const ProvisioningScreen({super.key, required this.payload});

  final QrPayload payload;

  @override
  State<ProvisioningScreen> createState() => _ProvisioningScreenState();
}

class _ProvisioningScreenState extends State<ProvisioningScreen> {
  final _api = const DeviceApi();
  final _formKey = GlobalKey<FormState>();
  late final TextEditingController _setupIpController;
  final _deviceIpController = TextEditingController();
  final _ssidController = TextEditingController();
  final _passwordController = TextEditingController();

  bool _busy = false;
  bool _discoveringLanIp = false;
  String _status = 'Waiting for setup AP connection.';
  String? _error;
  Map<String, dynamic>? _provisioningInfo;

  @override
  void initState() {
    super.initState();
    _setupIpController = TextEditingController(text: widget.payload.setupIp);
  }

  @override
  void dispose() {
    _setupIpController.dispose();
    _deviceIpController.dispose();
    _ssidController.dispose();
    _passwordController.dispose();
    super.dispose();
  }

  Future<void> _checkSetupAp() async {
    setState(() {
      _busy = true;
      _error = null;
      _status = 'Checking setup AP...';
    });

    try {
      final info = await _api.fetchProvisioningInfo(
        _setupIpController.text.trim(),
      );
      if (!mounted) {
        return;
      }
      setState(() {
        _provisioningInfo = info;
        _status = 'Setup AP is reachable.';
      });
    } catch (error) {
      if (!mounted) {
        return;
      }
      setState(() {
        _error =
            'Could not reach the setup AP. Join ${widget.payload.setupAp} on your phone first.';
        _status = 'Setup AP is not reachable yet.';
      });
    } finally {
      if (mounted) {
        setState(() {
          _busy = false;
        });
      }
    }
  }

  Future<void> _submit() async {
    if (!_formKey.currentState!.validate()) {
      return;
    }

    setState(() {
      _busy = true;
      _error = null;
      _status = 'Sending Wi-Fi credentials to the controller...';
    });

    final setupIp = _setupIpController.text.trim();
    final ssid = _ssidController.text.trim();
    final password = _passwordController.text;

    try {
      await _sendProvisioningRequest(setupIp, ssid, password);
      final device = await _pollProvisioningResult(setupIp);
      if (!mounted) {
        return;
      }
      Navigator.of(context).pop(device);
    } catch (error) {
      if (!mounted) {
        return;
      }
      setState(() {
        _error = error.toString().replaceFirst('Exception: ', '');
        _status = 'Provisioning failed.';
      });
    } finally {
      if (mounted) {
        setState(() {
          _busy = false;
        });
      }
    }
  }

  Future<void> _sendProvisioningRequest(
    String setupIp,
    String ssid,
    String password,
  ) async {
    try {
      await _api.configureProvisioning(
        host: setupIp,
        ssid: ssid,
        password: password,
      );
    } on TimeoutException {
      _enterLanRecoveryMode(
        'The setup AP dropped before the app received a response. The controller may already be joining your home Wi-Fi.',
      );
    } on SocketException {
      _enterLanRecoveryMode(
        'The setup AP disconnected while the controller switched to your home Wi-Fi. Reconnect your phone to the home Wi-Fi and verify the controller IP below.',
      );
    }
  }

  void _enterLanRecoveryMode(String message) {
    if (!mounted) {
      return;
    }

    setState(() {
      _status = 'Controller handoff in progress.';
      _error = message;
    });

    unawaited(_attemptAutoDetectLanIp());
  }

  Future<void> _attemptAutoDetectLanIp() async {
    if (_discoveringLanIp || !mounted) {
      return;
    }

    setState(() {
      _discoveringLanIp = true;
      _status = 'Trying to auto-detect the controller on your home Wi-Fi...';
    });

    try {
      final detectedIp = await _scanCommonLanRanges(widget.payload.deviceId);
      if (!mounted || detectedIp == null) {
        return;
      }

      _deviceIpController.text = detectedIp;
      final info = await _api.fetchInfo(detectedIp);
      final device = DeviceRecord(
        deviceId: (info['deviceId'] ?? widget.payload.deviceId) as String,
        deviceName:
            (info['deviceName'] ?? 'Vertical Farm Controller') as String,
        model: widget.payload.model,
        setupAp: widget.payload.setupAp,
        setupIp: widget.payload.setupIp,
        lastKnownIp: detectedIp,
      );
      if (!mounted) {
        return;
      }
      Navigator.of(context).pop(device);
    } finally {
      if (mounted) {
        setState(() {
          _discoveringLanIp = false;
          if (_status.startsWith('Trying to auto-detect')) {
            _status = 'Controller handoff in progress.';
          }
        });
      }
    }
  }

  Future<String?> _scanCommonLanRanges(String expectedDeviceId) async {
    const prefixes = ['192.168.1.', '192.168.0.', '10.0.0.'];
    for (final prefix in prefixes) {
      final found = await _scanPrefix(prefix, expectedDeviceId);
      if (found != null) {
        return found;
      }
    }
    return null;
  }

  Future<String?> _scanPrefix(String prefix, String expectedDeviceId) async {
    const batchSize = 20;
    for (int start = 1; start <= 254; start += batchSize) {
      final futures = <Future<String?>>[];
      for (
        int offset = 0;
        offset < batchSize && start + offset <= 254;
        offset++
      ) {
        final ip = '$prefix${start + offset}';
        futures.add(_probeDeviceIp(ip, expectedDeviceId));
      }

      final results = await Future.wait(futures);
      for (final ip in results) {
        if (ip != null) {
          return ip;
        }
      }
    }
    return null;
  }

  Future<String?> _probeDeviceIp(String ip, String expectedDeviceId) async {
    try {
      final info = await _api
          .fetchInfo(ip)
          .timeout(const Duration(milliseconds: 900));
      final deviceId = (info['deviceId'] ?? '') as String;
      if (deviceId == expectedDeviceId) {
        return ip;
      }
    } catch (_) {
      return null;
    }
    return null;
  }

  Future<DeviceRecord> _pollProvisioningResult(String setupIp) async {
    for (var attempt = 0; attempt < 25; attempt++) {
      if (!mounted) {
        throw const ApiException('Provisioning was interrupted.');
      }

      setState(() {
        _status = 'Waiting for the controller to join your Wi-Fi...';
      });

      await Future<void>.delayed(const Duration(seconds: 2));
      Map<String, dynamic> result;
      try {
        result = await _api.fetchProvisioningResult(setupIp);
      } on TimeoutException {
        _enterLanRecoveryMode(
          'The controller likely left setup mode. Reconnect your phone to the home Wi-Fi and verify the controller IP below.',
        );
        rethrow;
      } on SocketException {
        _enterLanRecoveryMode(
          'The setup AP is gone, which usually means the controller joined your home Wi-Fi. Reconnect your phone to the home Wi-Fi and verify the controller IP below.',
        );
        rethrow;
      }
      final state = (result['state'] ?? 'connecting') as String;
      if (state == 'connected') {
        final ip = (result['ip'] ?? '') as String;
        final info = await _api.fetchInfo(ip);
        return DeviceRecord(
          deviceId: (info['deviceId'] ?? widget.payload.deviceId) as String,
          deviceName:
              (info['deviceName'] ?? 'Vertical Farm Controller') as String,
          model: widget.payload.model,
          setupAp: widget.payload.setupAp,
          setupIp: widget.payload.setupIp,
          lastKnownIp: ip,
        );
      }
      if (state == 'failed') {
        throw ApiException((result['reason'] ?? 'Connection failed') as String);
      }
    }

    throw const ApiException(
      'Timed out waiting for the controller to join your Wi-Fi.',
    );
  }

  Future<void> _verifyLanIp() async {
    final ip = _deviceIpController.text.trim();
    if (ip.isEmpty) {
      setState(() {
        _error = 'Enter the controller IP from your router scan or LCD first.';
      });
      return;
    }

    setState(() {
      _busy = true;
      _error = null;
      _status = 'Checking controller on your home Wi-Fi...';
    });

    try {
      final info = await _api.fetchInfo(ip);
      final device = DeviceRecord(
        deviceId: (info['deviceId'] ?? widget.payload.deviceId) as String,
        deviceName:
            (info['deviceName'] ?? 'Vertical Farm Controller') as String,
        model: widget.payload.model,
        setupAp: widget.payload.setupAp,
        setupIp: widget.payload.setupIp,
        lastKnownIp: ip,
      );
      if (!mounted) {
        return;
      }
      Navigator.of(context).pop(device);
    } catch (error) {
      if (!mounted) {
        return;
      }
      setState(() {
        _error =
            'Could not reach the controller at $ip. Make sure your phone is back on the home Wi-Fi.';
        _status = 'LAN verification failed.';
      });
    } finally {
      if (mounted) {
        setState(() {
          _busy = false;
        });
      }
    }
  }

  Widget _buildLanRecoverySection() {
    return Column(
      crossAxisAlignment: CrossAxisAlignment.start,
      children: [
        const SizedBox(height: 20),
        const Divider(),
        const SizedBox(height: 12),
        Text(
          'Open Existing Device',
          style: Theme.of(context).textTheme.titleMedium,
        ),
        const SizedBox(height: 8),
        const Text(
          'If the setup AP is missing because the controller is already on your home Wi-Fi, enter its LAN IP and open the dashboard directly.',
        ),
        const SizedBox(height: 12),
        TextFormField(
          controller: _deviceIpController,
          decoration: const InputDecoration(
            labelText: 'Controller LAN IP',
            hintText: '192.168.1.17',
          ),
        ),
        const SizedBox(height: 12),
        OutlinedButton.icon(
          onPressed: _busy ? null : _verifyLanIp,
          icon: const Icon(Icons.router),
          label: const Text('Open dashboard with this IP'),
        ),
        const SizedBox(height: 8),
        Text(
          _discoveringLanIp
              ? 'Auto-detect is scanning common home-network ranges...'
              : 'Auto-detect will also try common home-network ranges after provisioning handoff.',
          style: const TextStyle(color: Color(0xFF5E6B63)),
        ),
      ],
    );
  }

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      appBar: AppBar(title: const Text('Provision Device')),
      body: ListView(
        padding: const EdgeInsets.all(20),
        children: [
          _HeroCard(
            title: 'Connect to setup AP first',
            subtitle:
                'On both Android and iPhone, join ${widget.payload.setupAp} in Wi-Fi settings before checking the setup connection below.',
            icon: Icons.wifi,
          ),
          const SizedBox(height: 16),
          Card(
            child: Padding(
              padding: const EdgeInsets.all(16),
              child: Form(
                key: _formKey,
                child: Column(
                  crossAxisAlignment: CrossAxisAlignment.start,
                  children: [
                    Text(
                      'Setup connection',
                      style: Theme.of(context).textTheme.titleMedium,
                    ),
                    const SizedBox(height: 12),
                    TextFormField(
                      controller: _setupIpController,
                      decoration: const InputDecoration(
                        labelText: 'Setup IP',
                        hintText: '192.168.4.1',
                      ),
                      validator: (value) =>
                          (value == null || value.trim().isEmpty)
                          ? 'Enter the setup IP'
                          : null,
                    ),
                    const SizedBox(height: 12),
                    OutlinedButton.icon(
                      onPressed: _busy ? null : _checkSetupAp,
                      icon: const Icon(Icons.network_check),
                      label: const Text('Check setup AP'),
                    ),
                    if (_provisioningInfo != null) ...[
                      const SizedBox(height: 12),
                      _DetailRow(
                        label: 'Controller',
                        value:
                            (_provisioningInfo!['deviceName'] ?? 'Device')
                                as String,
                      ),
                      _DetailRow(
                        label: 'Status',
                        value:
                            (_provisioningInfo!['status'] ??
                                    'waiting_for_credentials')
                                as String,
                      ),
                    ],
                    const SizedBox(height: 20),
                    Text(
                      'Home Wi-Fi',
                      style: Theme.of(context).textTheme.titleMedium,
                    ),
                    const SizedBox(height: 12),
                    TextFormField(
                      controller: _ssidController,
                      decoration: const InputDecoration(
                        labelText: 'Wi-Fi SSID',
                      ),
                      validator: (value) =>
                          (value == null || value.trim().isEmpty)
                          ? 'Enter your Wi-Fi name'
                          : null,
                    ),
                    const SizedBox(height: 12),
                    TextFormField(
                      controller: _passwordController,
                      obscureText: true,
                      decoration: const InputDecoration(
                        labelText: 'Wi-Fi password',
                      ),
                    ),
                    const SizedBox(height: 16),
                    FilledButton.icon(
                      onPressed: _busy ? null : _submit,
                      icon: _busy
                          ? const SizedBox(
                              width: 16,
                              height: 16,
                              child: CircularProgressIndicator(strokeWidth: 2),
                            )
                          : const Icon(Icons.send),
                      label: const Text('Send credentials'),
                    ),
                    const SizedBox(height: 12),
                    Text(_status),
                    if (_error != null) ...[
                      const SizedBox(height: 8),
                      Text(
                        _error!,
                        style: TextStyle(
                          color: Theme.of(context).colorScheme.error,
                        ),
                      ),
                    ],
                    _buildLanRecoverySection(),
                  ],
                ),
              ),
            ),
          ),
        ],
      ),
    );
  }
}

class DashboardScreen extends StatefulWidget {
  const DashboardScreen({
    super.key,
    required this.device,
    required this.onDeviceUpdated,
    required this.onForgetDevice,
  });

  final DeviceRecord device;
  final Future<void> Function(DeviceRecord device) onDeviceUpdated;
  final Future<void> Function() onForgetDevice;

  @override
  State<DashboardScreen> createState() => _DashboardScreenState();
}

class _DashboardScreenState extends State<DashboardScreen> {
  final _api = const DeviceApi();
  Timer? _pollTimer;
  Map<String, dynamic>? _info;
  Map<String, dynamic>? _status;
  final Map<int, _ZoneHistory> _zoneHistory = {
    1: _ZoneHistory(),
    2: _ZoneHistory(),
  };
  String? _error;
  bool _busy = false;

  String get _host => widget.device.lastKnownIp;

  @override
  void initState() {
    super.initState();
    _refresh();
    _pollTimer = Timer.periodic(const Duration(seconds: 3), (_) {
      _refresh(silent: true);
    });
  }

  @override
  void dispose() {
    _pollTimer?.cancel();
    super.dispose();
  }

  Future<void> _refresh({bool silent = false}) async {
    if (!silent && mounted) {
      setState(() {
        _busy = true;
      });
    }

    try {
      final results = await Future.wait([
        _api.fetchInfo(_host),
        _api.fetchStatus(_host),
      ]);
      final info = results[0];
      final status = results[1];
      final newIp =
          ((status['wifi'] as Map<String, dynamic>?)?['ip'] ??
                  widget.device.lastKnownIp)
              as String;
      final updated = widget.device.copyWith(
        deviceName: (info['deviceName'] ?? widget.device.deviceName) as String,
        lastKnownIp: newIp,
      );
      await widget.onDeviceUpdated(updated);
      if (!mounted) {
        return;
      }
      _recordHistory(status);
      setState(() {
        _info = info;
        _status = status;
        _error = null;
      });
    } catch (error) {
      if (!mounted) {
        return;
      }
      setState(() {
        _error = error.toString().replaceFirst('Exception: ', '');
      });
    } finally {
      if (mounted) {
        setState(() {
          _busy = false;
        });
      }
    }
  }

  void _recordHistory(Map<String, dynamic> status) {
    final zones = (status['zones'] as List<dynamic>?) ?? const <dynamic>[];
    for (final entry in zones) {
      if (entry is! Map<String, dynamic>) {
        continue;
      }
      final zoneId = entry['zone'];
      if (zoneId is! num) {
        continue;
      }
      final history = _zoneHistory[zoneId.toInt()];
      if (history == null) {
        continue;
      }
      history.addSample(
        moisture: _asDouble(entry['soilMoisturePct']),
        temperature: _asDouble(entry['tempC']),
        humidity: _asDouble(entry['humidityPct']),
        nutrient: _asDouble(entry['nutrientPpm']),
      );
    }
  }

  Future<void> _resetWifi() async {
    final confirmed = await showDialog<bool>(
      context: context,
      builder: (context) => AlertDialog(
        title: const Text('Reset Wi-Fi'),
        content: const Text(
          'The device will clear saved Wi-Fi credentials and reboot back into setup mode.',
        ),
        actions: [
          TextButton(
            onPressed: () => Navigator.of(context).pop(false),
            child: const Text('Cancel'),
          ),
          FilledButton(
            onPressed: () => Navigator.of(context).pop(true),
            child: const Text('Reset'),
          ),
        ],
      ),
    );

    if (confirmed != true) {
      return;
    }

    try {
      await _api.resetWifi(_host);
      await widget.onForgetDevice();
    } catch (error) {
      if (!mounted) {
        return;
      }
      ScaffoldMessenger.of(context).showSnackBar(
        SnackBar(
          content: Text(error.toString().replaceFirst('Exception: ', '')),
        ),
      );
    }
  }

  @override
  Widget build(BuildContext context) {
    final status = _status;
    final wifi =
        (status?['wifi'] as Map<String, dynamic>?) ?? const <String, dynamic>{};
    final zones = (status?['zones'] as List<dynamic>?) ?? const <dynamic>[];
    final isOnline = (wifi['connected'] ?? false) == true;

    return DefaultTabController(
      length: 3,
      child: Scaffold(
        appBar: AppBar(
          title: Text(widget.device.deviceName),
          backgroundColor: const Color(0xFFF4F7F1),
          bottom: const TabBar(
            tabs: [
              Tab(text: 'Overview'),
              Tab(text: 'Zones'),
              Tab(text: 'Device'),
            ],
          ),
          actions: [
            IconButton(
              onPressed: _busy ? null : _refresh,
              icon: const Icon(Icons.refresh),
            ),
          ],
        ),
        body: Column(
          children: [
            Material(
              color: isOnline
                  ? const Color(0xFFDDEFE3)
                  : const Color(0xFFF6DFDF),
              child: ListTile(
                leading: Icon(isOnline ? Icons.check_circle : Icons.wifi_off),
                title: Text(isOnline ? 'Device online' : 'Device offline'),
                subtitle: Text(
                  _error ??
                      'Polling ${widget.device.lastKnownIp} every 3 seconds',
                ),
              ),
            ),
            Expanded(
              child: TabBarView(
                children: [
                  _OverviewTab(status: status, histories: _zoneHistory),
                  _ZonesTab(zones: zones, histories: _zoneHistory),
                  _DeviceTab(
                    device: widget.device,
                    info: _info,
                    status: status,
                    onResetWifi: _resetWifi,
                  ),
                ],
              ),
            ),
          ],
        ),
      ),
    );
  }
}

class _OverviewTab extends StatelessWidget {
  const _OverviewTab({required this.status, required this.histories});

  final Map<String, dynamic>? status;
  final Map<int, _ZoneHistory> histories;

  @override
  Widget build(BuildContext context) {
    final data = status ?? const <String, dynamic>{};
    final wifi =
        (data['wifi'] as Map<String, dynamic>?) ?? const <String, dynamic>{};
    final zones = (data['zones'] as List<dynamic>?) ?? const <dynamic>[];
    final phase = '${data['phase'] ?? '--'}';
    final cycle = '${data['cycle'] ?? '--'}';
    final connected = (wifi['connected'] ?? false) == true;
    final avgMoisture = _averageZoneValue(zones, 'soilMoisturePct');
    final avgNutrient = _averageZoneValue(zones, 'nutrientPpm');
    final phaseProgress = _phaseProgress(data);
    final tankDistance = _asDouble(data['tankDistanceCm']);
    final tankPercent = _tankPercent(
      tankDistance,
      (data['tankLow'] ?? false) == true,
    );
    final sampleAge = _sampleAgeText(data['sampleAgeMs']);
    final moistureTrend = _combineSeries(
      histories[1]?.moisture ?? const <double>[],
      histories[2]?.moisture ?? const <double>[],
    );
    final tempTrend = _combineSeries(
      histories[1]?.temperature ?? const <double>[],
      histories[2]?.temperature ?? const <double>[],
    );

    return ListView(
      padding: const EdgeInsets.all(16),
      children: [
        _SectionCard(
          backgroundColor: const Color(0xFF173A2E),
          padding: const EdgeInsets.all(20),
          child: Column(
            crossAxisAlignment: CrossAxisAlignment.start,
            children: [
              Wrap(
                spacing: 8,
                runSpacing: 8,
                children: [
                  _StatusChip(
                    label: 'Connection',
                    value: connected ? 'Online' : 'Offline',
                  ),
                  _StatusChip(label: 'Phase', value: phase),
                  _StatusChip(label: 'Cycle', value: cycle),
                  _StatusChip(label: 'Last update', value: sampleAge),
                ],
              ),
              const SizedBox(height: 18),
              Text(
                'Overview',
                style: Theme.of(context).textTheme.headlineSmall?.copyWith(
                  color: Colors.white,
                  fontWeight: FontWeight.w700,
                ),
              ),
              const SizedBox(height: 8),
              Text(
                'Automated Solar-Powered IoT-Based Monitoring and Nutrient Regulation System for Vertical Urban Farming Using Arduino-Based Sensors',
                style: const TextStyle(color: Color(0xFFD9ECE2), height: 1.35),
              ),
              const SizedBox(height: 20),
              _ProgressStrip(
                label: 'Phase progress',
                value: phaseProgress,
                accentColor: const Color(0xFF67D39B),
                valueText: '${(phaseProgress * 100).round()}%',
              ),
            ],
          ),
        ),
        const SizedBox(height: 16),
        Row(
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            Expanded(
              child: _SpotlightCard(
                title: 'Average soil wetness',
                value: avgMoisture == null ? '--' : '${avgMoisture.round()}%',
                subtitle: avgMoisture == null
                    ? 'Waiting for live readings'
                    : _moistureBandLabel(avgMoisture),
                progress: avgMoisture == null
                    ? 0
                    : (avgMoisture / 100).clamp(0, 1),
                accentColor: const Color(0xFF2F8F62),
              ),
            ),
            const SizedBox(width: 12),
            Expanded(
              child: Column(
                children: [
                  _SummaryStatCard(
                    title: 'Plant food',
                    value: avgNutrient == null
                        ? '--'
                        : '${avgNutrient.round()} ppm',
                    subtitle: avgNutrient == null
                        ? 'No sample'
                        : _nutrientBandLabel(avgNutrient),
                  ),
                  const SizedBox(height: 12),
                  _SummaryStatCard(
                    title: 'Time left',
                    value: _phaseRemainingText(data),
                    subtitle: 'Current phase countdown',
                  ),
                ],
              ),
            ),
          ],
        ),
        const SizedBox(height: 16),
        _SectionCard(
          child: Column(
            crossAxisAlignment: CrossAxisAlignment.start,
            children: [
              _SectionHeader(
                title: 'Water tank',
                subtitle: (data['tankLow'] ?? false) == true
                    ? 'Needs attention soon'
                    : 'Operating normally',
              ),
              const SizedBox(height: 16),
              Row(
                children: [
                  _TankVisual(
                    fillPercent: tankPercent,
                    isLow: (data['tankLow'] ?? false) == true,
                  ),
                  const SizedBox(width: 20),
                  Expanded(
                    child: Column(
                      crossAxisAlignment: CrossAxisAlignment.start,
                      children: [
                        Text(
                          tankDistance == null
                              ? 'Distance unavailable'
                              : '${tankDistance.toStringAsFixed(1)} cm',
                          style: Theme.of(context).textTheme.headlineSmall
                              ?.copyWith(fontWeight: FontWeight.w700),
                        ),
                        const SizedBox(height: 6),
                        Text(
                          (data['tankLow'] ?? false) == true
                              ? 'Water level is below the safe range. Refill soon.'
                              : 'Water level is in the safe range for the current cycle.',
                        ),
                        const SizedBox(height: 14),
                        _ProgressStrip(
                          label: 'Freshness',
                          value: _freshnessValue(data['sampleAgeMs']),
                          accentColor: const Color(0xFF3B82F6),
                          valueText: sampleAge,
                        ),
                      ],
                    ),
                  ),
                ],
              ),
            ],
          ),
        ),
        const SizedBox(height: 16),
        _SectionCard(
          child: Column(
            crossAxisAlignment: CrossAxisAlignment.start,
            children: [
              const _SectionHeader(
                title: 'Quick trends',
                subtitle: 'Short in-app history built from live polling',
              ),
              const SizedBox(height: 16),
              _MiniChartCard(
                title: 'Soil wetness trend',
                subtitle: 'Average of both zones',
                series: moistureTrend,
                color: const Color(0xFF2F8F62),
                suffix: '%',
              ),
              const SizedBox(height: 12),
              _MiniChartCard(
                title: 'Temperature trend',
                subtitle: 'Average of both zones',
                series: tempTrend,
                color: const Color(0xFFF97316),
                suffix: ' C',
              ),
            ],
          ),
        ),
        const SizedBox(height: 16),
        _SectionCard(
          child: Column(
            crossAxisAlignment: CrossAxisAlignment.start,
            children: [
              const _SectionHeader(
                title: 'Actuator state',
                subtitle: 'Current output state from the controller',
              ),
              const SizedBox(height: 16),
              Row(
                children: [
                  Expanded(
                    child: _SummaryStatCard(
                      title: 'Water valve',
                      value: (data['waterValveOpen'] ?? false) == true
                          ? 'Open'
                          : 'Closed',
                      subtitle: 'Relay channel 1',
                    ),
                  ),
                  const SizedBox(width: 12),
                  Expanded(
                    child: _SummaryStatCard(
                      title: 'Nutrient valve',
                      value: (data['nutrientValveOpen'] ?? false) == true
                          ? 'Open'
                          : 'Closed',
                      subtitle: 'Relay channel 2',
                    ),
                  ),
                ],
              ),
            ],
          ),
        ),
        const SizedBox(height: 16),
        _SectionCard(
          child: Column(
            crossAxisAlignment: CrossAxisAlignment.start,
            children: [
              const _SectionHeader(
                title: 'Latest warning',
                subtitle: 'Most recent alert generated by the controller',
              ),
              const SizedBox(height: 8),
              Text('${data['lastAlert'] ?? 'No alerts yet'}'),
            ],
          ),
        ),
      ],
    );
  }
}

class _ZonesTab extends StatelessWidget {
  const _ZonesTab({required this.zones, required this.histories});

  final List<dynamic> zones;
  final Map<int, _ZoneHistory> histories;

  @override
  Widget build(BuildContext context) {
    if (zones.isEmpty) {
      return const Center(child: Text('No zone telemetry yet.'));
    }

    return ListView.builder(
      padding: const EdgeInsets.all(16),
      itemCount: zones.length,
      itemBuilder: (context, index) {
        final zone = zones[index] as Map<String, dynamic>;
        return Card(
          margin: const EdgeInsets.only(bottom: 16),
          color: const Color(0xFFFDFDFB),
          elevation: 0,
          shape: RoundedRectangleBorder(
            borderRadius: BorderRadius.circular(24),
          ),
          child: Padding(
            padding: const EdgeInsets.all(18),
            child: Column(
              crossAxisAlignment: CrossAxisAlignment.start,
              children: [
                Row(
                  children: [
                    Expanded(
                      child: Text(
                        'Zone ${zone['zone'] ?? index + 1}',
                        style: Theme.of(context).textTheme.titleLarge?.copyWith(
                          fontWeight: FontWeight.w700,
                        ),
                      ),
                    ),
                    _BandPill(
                      label: '${zone['moistureBand'] ?? 'Unknown'}',
                      color: _bandColor('${zone['moistureBand'] ?? ''}'),
                    ),
                  ],
                ),
                const SizedBox(height: 12),
                Row(
                  children: [
                    Expanded(
                      child: _MetricGauge(
                        label: 'Soil moisture',
                        value: _asDouble(zone['soilMoisturePct']),
                        display: '${zone['soilMoisturePct'] ?? '--'} %',
                        progress:
                            ((_asDouble(zone['soilMoisturePct']) ?? 0) / 100)
                                .clamp(0, 1),
                        color: const Color(0xFF2F8F62),
                      ),
                    ),
                    const SizedBox(width: 12),
                    Expanded(
                      child: _MetricGauge(
                        label: 'Temperature',
                        value: _asDouble(zone['tempC']),
                        display: '${zone['tempC'] ?? '--'} C',
                        progress: ((_asDouble(zone['tempC']) ?? 0) / 50).clamp(
                          0,
                          1,
                        ),
                        color: const Color(0xFFF97316),
                      ),
                    ),
                  ],
                ),
                const SizedBox(height: 12),
                Row(
                  children: [
                    Expanded(
                      child: _MetricGauge(
                        label: 'Humidity',
                        value: _asDouble(zone['humidityPct']),
                        display: '${zone['humidityPct'] ?? '--'} %',
                        progress: ((_asDouble(zone['humidityPct']) ?? 0) / 100)
                            .clamp(0, 1),
                        color: const Color(0xFF3B82F6),
                      ),
                    ),
                    const SizedBox(width: 12),
                    Expanded(
                      child: _MetricGauge(
                        label: 'Plant food',
                        value: _asDouble(zone['nutrientPpm']),
                        display: '${zone['nutrientPpm'] ?? '--'} ppm',
                        progress: ((_asDouble(zone['nutrientPpm']) ?? 0) / 1600)
                            .clamp(0, 1),
                        color: const Color(0xFFA855F7),
                      ),
                    ),
                  ],
                ),
                const SizedBox(height: 16),
                _MiniChartCard(
                  title: 'Soil wetness over time',
                  subtitle: 'Recent samples from app polling',
                  series:
                      histories[(zone['zone'] as num?)?.toInt()]?.moisture ??
                      const <double>[],
                  color: const Color(0xFF2F8F62),
                  suffix: '%',
                ),
                const SizedBox(height: 12),
                _MiniChartCard(
                  title: 'Temperature over time',
                  subtitle: 'Recent air temperature samples',
                  series:
                      histories[(zone['zone'] as num?)?.toInt()]?.temperature ??
                      const <double>[],
                  color: const Color(0xFFF97316),
                  suffix: ' C',
                ),
                const SizedBox(height: 16),
                Container(
                  decoration: BoxDecoration(
                    color: const Color(0xFFF1F5EF),
                    borderRadius: BorderRadius.circular(18),
                  ),
                  padding: const EdgeInsets.all(14),
                  child: Row(
                    children: [
                      Expanded(
                        child: _NpkCell(
                          label: 'N',
                          value:
                              '${(zone['npk'] as Map<String, dynamic>?)?['n'] ?? '--'}',
                        ),
                      ),
                      Expanded(
                        child: _NpkCell(
                          label: 'P',
                          value:
                              '${(zone['npk'] as Map<String, dynamic>?)?['p'] ?? '--'}',
                        ),
                      ),
                      Expanded(
                        child: _NpkCell(
                          label: 'K',
                          value:
                              '${(zone['npk'] as Map<String, dynamic>?)?['k'] ?? '--'}',
                        ),
                      ),
                    ],
                  ),
                ),
              ],
            ),
          ),
        );
      },
    );
  }
}

class _DeviceTab extends StatelessWidget {
  const _DeviceTab({
    required this.device,
    required this.info,
    required this.status,
    required this.onResetWifi,
  });

  final DeviceRecord device;
  final Map<String, dynamic>? info;
  final Map<String, dynamic>? status;
  final Future<void> Function() onResetWifi;

  @override
  Widget build(BuildContext context) {
    final wifi =
        (status?['wifi'] as Map<String, dynamic>?) ?? const <String, dynamic>{};
    return ListView(
      padding: const EdgeInsets.all(16),
      children: [
        _SectionCard(
          child: Padding(
            padding: const EdgeInsets.all(4),
            child: Column(
              crossAxisAlignment: CrossAxisAlignment.start,
              children: [
                const _SectionHeader(
                  title: 'Device identity',
                  subtitle: 'Saved companion-app record for this controller',
                ),
                const SizedBox(height: 12),
                _DetailRow(label: 'Device name', value: device.deviceName),
                _DetailRow(label: 'Device ID', value: device.deviceId),
                _DetailRow(label: 'Model', value: device.model),
                _DetailRow(label: 'Last known IP', value: device.lastKnownIp),
                _DetailRow(label: 'Setup AP', value: device.setupAp),
                _DetailRow(label: 'Setup IP', value: device.setupIp),
              ],
            ),
          ),
        ),
        const SizedBox(height: 16),
        _SectionCard(
          child: Padding(
            padding: const EdgeInsets.all(4),
            child: Column(
              crossAxisAlignment: CrossAxisAlignment.start,
              children: [
                const _SectionHeader(
                  title: 'Network',
                  subtitle:
                      'Current controller connectivity and firmware details',
                ),
                const SizedBox(height: 12),
                _DetailRow(
                  label: 'Wi-Fi state',
                  value: (wifi['connected'] ?? false) == true
                      ? 'Connected'
                      : 'Disconnected',
                ),
                _DetailRow(
                  label: 'Wi-Fi SSID',
                  value: '${wifi['ssid'] ?? '--'}',
                ),
                _DetailRow(label: 'Current IP', value: '${wifi['ip'] ?? '--'}'),
                _DetailRow(label: 'RSSI', value: '${wifi['rssi'] ?? '--'}'),
                _DetailRow(
                  label: 'Firmware',
                  value: '${info?['firmwareVersion'] ?? '--'}',
                ),
                _DetailRow(
                  label: 'API',
                  value: '${info?['apiVersion'] ?? '--'}',
                ),
              ],
            ),
          ),
        ),
        const SizedBox(height: 16),
        _SectionCard(
          child: Column(
            crossAxisAlignment: CrossAxisAlignment.start,
            children: [
              const _SectionHeader(
                title: 'Recovery',
                subtitle:
                    'Return the controller to setup mode when you need a new router or password',
              ),
              const SizedBox(height: 12),
              FilledButton.tonalIcon(
                onPressed: onResetWifi,
                icon: const Icon(Icons.restart_alt),
                label: const Text('Reset Wi-Fi and return to setup mode'),
              ),
            ],
          ),
        ),
      ],
    );
  }
}

class QrScannerScreen extends StatefulWidget {
  const QrScannerScreen({super.key});

  @override
  State<QrScannerScreen> createState() => _QrScannerScreenState();
}

class _QrScannerScreenState extends State<QrScannerScreen> {
  bool _handled = false;

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      appBar: AppBar(title: const Text('Scan Device QR')),
      body: Stack(
        children: [
          MobileScanner(
            onDetect: (capture) {
              if (_handled) {
                return;
              }
              final raw = capture.barcodes.first.rawValue;
              if (raw == null || raw.isEmpty) {
                return;
              }
              _handled = true;
              Navigator.of(context).pop(raw);
            },
          ),
          const Align(
            alignment: Alignment.bottomCenter,
            child: Padding(
              padding: EdgeInsets.all(24),
              child: Card(
                child: Padding(
                  padding: EdgeInsets.all(16),
                  child: Text(
                    'Center the printed QR sticker in the camera view.',
                  ),
                ),
              ),
            ),
          ),
        ],
      ),
    );
  }
}

class _HeroCard extends StatelessWidget {
  const _HeroCard({
    required this.title,
    required this.subtitle,
    required this.icon,
  });

  final String title;
  final String subtitle;
  final IconData icon;

  @override
  Widget build(BuildContext context) {
    return Card(
      color: const Color(0xFF1F5A39),
      child: Padding(
        padding: const EdgeInsets.all(20),
        child: Row(
          children: [
            Icon(icon, color: Colors.white, size: 36),
            const SizedBox(width: 16),
            Expanded(
              child: Column(
                crossAxisAlignment: CrossAxisAlignment.start,
                children: [
                  Text(
                    title,
                    style: Theme.of(
                      context,
                    ).textTheme.titleLarge?.copyWith(color: Colors.white),
                  ),
                  const SizedBox(height: 6),
                  Text(subtitle, style: const TextStyle(color: Colors.white70)),
                ],
              ),
            ),
          ],
        ),
      ),
    );
  }
}

class _SectionCard extends StatelessWidget {
  const _SectionCard({
    required this.child,
    this.backgroundColor = Colors.white,
    this.padding = const EdgeInsets.all(16),
  });

  final Widget child;
  final Color backgroundColor;
  final EdgeInsets padding;

  @override
  Widget build(BuildContext context) {
    return Container(
      decoration: BoxDecoration(
        color: backgroundColor,
        borderRadius: BorderRadius.circular(24),
        boxShadow: const [
          BoxShadow(
            color: Color(0x14000000),
            blurRadius: 18,
            offset: Offset(0, 8),
          ),
        ],
      ),
      child: Padding(padding: padding, child: child),
    );
  }
}

class _SectionHeader extends StatelessWidget {
  const _SectionHeader({required this.title, required this.subtitle});

  final String title;
  final String subtitle;

  @override
  Widget build(BuildContext context) {
    return Column(
      crossAxisAlignment: CrossAxisAlignment.start,
      children: [
        Text(
          title,
          style: Theme.of(
            context,
          ).textTheme.titleLarge?.copyWith(fontWeight: FontWeight.w700),
        ),
        const SizedBox(height: 4),
        Text(
          subtitle,
          style: const TextStyle(color: Color(0xFF5E6B63), height: 1.35),
        ),
      ],
    );
  }
}

class _StatusChip extends StatelessWidget {
  const _StatusChip({required this.label, required this.value});

  final String label;
  final String value;

  @override
  Widget build(BuildContext context) {
    return Container(
      padding: const EdgeInsets.symmetric(horizontal: 12, vertical: 8),
      decoration: BoxDecoration(
        color: const Color(0x1FFFFFFF),
        borderRadius: BorderRadius.circular(999),
        border: Border.all(color: const Color(0x33FFFFFF)),
      ),
      child: RichText(
        text: TextSpan(
          style: const TextStyle(color: Colors.white),
          children: [
            TextSpan(
              text: '$label: ',
              style: const TextStyle(color: Color(0xFFD4E5DC)),
            ),
            TextSpan(
              text: value,
              style: const TextStyle(fontWeight: FontWeight.w700),
            ),
          ],
        ),
      ),
    );
  }
}

class _ProgressStrip extends StatelessWidget {
  const _ProgressStrip({
    required this.label,
    required this.value,
    required this.accentColor,
    required this.valueText,
  });

  final String label;
  final double value;
  final Color accentColor;
  final String valueText;

  @override
  Widget build(BuildContext context) {
    final clamped = value.clamp(0, 1).toDouble();
    return Column(
      crossAxisAlignment: CrossAxisAlignment.start,
      children: [
        Row(
          children: [
            Expanded(
              child: Text(
                label,
                style: const TextStyle(color: Color(0xFFD9ECE2)),
              ),
            ),
            Text(
              valueText,
              style: const TextStyle(
                color: Colors.white,
                fontWeight: FontWeight.w700,
              ),
            ),
          ],
        ),
        const SizedBox(height: 8),
        ClipRRect(
          borderRadius: BorderRadius.circular(999),
          child: LinearProgressIndicator(
            minHeight: 10,
            value: clamped,
            backgroundColor: const Color(0x1FFFFFFF),
            valueColor: AlwaysStoppedAnimation<Color>(accentColor),
          ),
        ),
      ],
    );
  }
}
class _SpotlightCard extends StatelessWidget {
  const _SpotlightCard({
    required this.title,
    required this.value,
    required this.subtitle,
    required this.progress,
    required this.accentColor,
  });

  final String title;
  final String value;
  final String subtitle;
  final double progress;
  final Color accentColor;

  @override
  Widget build(BuildContext context) {
    return _SectionCard(
      child: Column(
        mainAxisSize: MainAxisSize.min,
        children: [
          Text(
            title,
            textAlign: TextAlign.center,
            style: Theme.of(context).textTheme.titleMedium?.copyWith(
              fontWeight: FontWeight.w700,
            ),
          ),
          const SizedBox(height: 12),
          const Text(
            'Current',
            textAlign: TextAlign.center,
            style: TextStyle(
              color: Color(0xFF5E6B63),
              fontSize: 13,
            ),
          ),
          const SizedBox(height: 4),
          Text(
            value,
            textAlign: TextAlign.center,
            maxLines: 1,
            overflow: TextOverflow.ellipsis,
            style: Theme.of(context).textTheme.headlineMedium?.copyWith(
              fontWeight: FontWeight.w800,
              height: 1.0,
            ),
          ),
          const SizedBox(height: 14),
          SizedBox(
            width: 96,
            height: 96,
            child: CircularProgressIndicator(
              value: progress.clamp(0, 1).toDouble(),
              strokeWidth: 10,
              backgroundColor: const Color(0xFFE4ECE6),
              valueColor: AlwaysStoppedAnimation<Color>(accentColor),
            ),
          ),
          const SizedBox(height: 14),
          Text(
            subtitle,
            textAlign: TextAlign.center,
            style: const TextStyle(
              color: Color(0xFF5E6B63),
            ),
          ),
        ],
      ),
    );
  }
}

class _SummaryStatCard extends StatelessWidget {
  const _SummaryStatCard({
    required this.title,
    required this.value,
    required this.subtitle,
  });

  final String title;
  final String value;
  final String subtitle;

  @override
  Widget build(BuildContext context) {
    return _SectionCard(
      padding: const EdgeInsets.all(14),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          Text(title, style: const TextStyle(color: Color(0xFF5E6B63))),
          const SizedBox(height: 10),
          Text(
            value,
            style: Theme.of(
              context,
            ).textTheme.titleLarge?.copyWith(fontWeight: FontWeight.w800),
          ),
          const SizedBox(height: 6),
          Text(subtitle, style: const TextStyle(color: Color(0xFF5E6B63))),
        ],
      ),
    );
  }
}

class _TankVisual extends StatelessWidget {
  const _TankVisual({required this.fillPercent, required this.isLow});

  final double fillPercent;
  final bool isLow;

  @override
  Widget build(BuildContext context) {
    final fill = fillPercent.clamp(0, 1).toDouble();
    return SizedBox(
      width: 86,
      height: 160,
      child: Stack(
        alignment: Alignment.bottomCenter,
        children: [
          Positioned(
            top: 0,
            child: Container(
              width: 42,
              height: 12,
              decoration: BoxDecoration(
                color: const Color(0xFFB7C8BB),
                borderRadius: BorderRadius.circular(8),
              ),
            ),
          ),
          Container(
            width: 78,
            height: 140,
            decoration: BoxDecoration(
              borderRadius: BorderRadius.circular(18),
              border: Border.all(color: const Color(0xFF7C9685), width: 2),
              color: const Color(0xFFF5F8F4),
            ),
            child: Align(
              alignment: Alignment.bottomCenter,
              child: AnimatedContainer(
                duration: const Duration(milliseconds: 400),
                width: double.infinity,
                height: 136 * fill,
                decoration: BoxDecoration(
                  color: isLow
                      ? const Color(0xFFF59E0B)
                      : const Color(0xFF2F8F62),
                  borderRadius: const BorderRadius.vertical(
                    bottom: Radius.circular(16),
                  ),
                ),
              ),
            ),
          ),
        ],
      ),
    );
  }
}

class _MiniChartCard extends StatelessWidget {
  const _MiniChartCard({
    required this.title,
    required this.subtitle,
    required this.series,
    required this.color,
    required this.suffix,
  });

  final String title;
  final String subtitle;
  final List<double> series;
  final Color color;
  final String suffix;

  @override
  Widget build(BuildContext context) {
    final latest = series.isEmpty
        ? '--'
        : '${series.last.toStringAsFixed(0)}$suffix';
    return Container(
      decoration: BoxDecoration(
        color: const Color(0xFFF7FAF7),
        borderRadius: BorderRadius.circular(18),
      ),
      padding: const EdgeInsets.all(14),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          Row(
            children: [
              Expanded(
                child: Column(
                  crossAxisAlignment: CrossAxisAlignment.start,
                  children: [
                    Text(
                      title,
                      style: Theme.of(context).textTheme.titleMedium?.copyWith(
                        fontWeight: FontWeight.w700,
                      ),
                    ),
                    const SizedBox(height: 4),
                    Text(
                      subtitle,
                      style: const TextStyle(color: Color(0xFF5E6B63)),
                    ),
                  ],
                ),
              ),
              Text(
                latest,
                style: Theme.of(context).textTheme.titleMedium?.copyWith(
                  fontWeight: FontWeight.w800,
                  color: color,
                ),
              ),
            ],
          ),
          const SizedBox(height: 14),
          SizedBox(
            height: 120,
            child: CustomPaint(
              painter: _SparklinePainter(series: series, color: color),
              child: Container(),
            ),
          ),
        ],
      ),
    );
  }
}

class _BandPill extends StatelessWidget {
  const _BandPill({required this.label, required this.color});

  final String label;
  final Color color;

  @override
  Widget build(BuildContext context) {
    return Container(
      padding: const EdgeInsets.symmetric(horizontal: 10, vertical: 6),
      decoration: BoxDecoration(
        color: color.withValues(alpha: 0.12),
        borderRadius: BorderRadius.circular(999),
      ),
      child: Text(
        label,
        style: TextStyle(color: color, fontWeight: FontWeight.w700),
      ),
    );
  }
}

class _MetricGauge extends StatelessWidget {
  const _MetricGauge({
    required this.label,
    required this.value,
    required this.display,
    required this.progress,
    required this.color,
  });

  final String label;
  final double? value;
  final String display;
  final double progress;
  final Color color;

  @override
  Widget build(BuildContext context) {
    return Container(
      decoration: BoxDecoration(
        color: const Color(0xFFF7FAF7),
        borderRadius: BorderRadius.circular(18),
      ),
      padding: const EdgeInsets.all(14),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          Text(label, style: const TextStyle(color: Color(0xFF5E6B63))),
          const SizedBox(height: 8),
          Text(
            display,
            style: Theme.of(
              context,
            ).textTheme.titleMedium?.copyWith(fontWeight: FontWeight.w800),
          ),
          const SizedBox(height: 10),
          ClipRRect(
            borderRadius: BorderRadius.circular(999),
            child: LinearProgressIndicator(
              minHeight: 10,
              value: progress.clamp(0, 1),
              backgroundColor: const Color(0xFFE4ECE6),
              valueColor: AlwaysStoppedAnimation<Color>(color),
            ),
          ),
        ],
      ),
    );
  }
}

class _NpkCell extends StatelessWidget {
  const _NpkCell({required this.label, required this.value});

  final String label;
  final String value;

  @override
  Widget build(BuildContext context) {
    return Column(
      children: [
        Text(label, style: const TextStyle(color: Color(0xFF5E6B63))),
        const SizedBox(height: 6),
        Text(
          value,
          style: Theme.of(
            context,
          ).textTheme.titleLarge?.copyWith(fontWeight: FontWeight.w800),
        ),
      ],
    );
  }
}

class _ZoneHistory {
  static const int maxSamples = 24;

  final List<double> moisture = <double>[];
  final List<double> temperature = <double>[];
  final List<double> humidity = <double>[];
  final List<double> nutrient = <double>[];

  void addSample({
    double? moisture,
    double? temperature,
    double? humidity,
    double? nutrient,
  }) {
    _push(this.moisture, moisture);
    _push(this.temperature, temperature);
    _push(this.humidity, humidity);
    _push(this.nutrient, nutrient);
  }

  void _push(List<double> target, double? value) {
    if (value == null || !value.isFinite) {
      return;
    }
    target.add(value);
    if (target.length > maxSamples) {
      target.removeAt(0);
    }
  }
}

class _SparklinePainter extends CustomPainter {
  const _SparklinePainter({required this.series, required this.color});

  final List<double> series;
  final Color color;

  @override
  void paint(Canvas canvas, Size size) {
    final gridPaint = Paint()
      ..color = const Color(0xFFE4ECE6)
      ..strokeWidth = 1;
    for (int i = 1; i <= 3; i++) {
      final y = size.height * i / 4;
      canvas.drawLine(Offset(0, y), Offset(size.width, y), gridPaint);
    }

    if (series.length < 2) {
      return;
    }

    final minValue = series.reduce((a, b) => a < b ? a : b);
    final maxValue = series.reduce((a, b) => a > b ? a : b);
    final valueRange = (maxValue - minValue).abs() < 0.001
        ? 1.0
        : (maxValue - minValue);
    final dx = size.width / (series.length - 1);

    final fillPath = Path();
    final linePath = Path();
    for (int i = 0; i < series.length; i++) {
      final x = i * dx;
      final normalized = (series[i] - minValue) / valueRange;
      final y = size.height - (normalized * (size.height - 8)) - 4;
      if (i == 0) {
        linePath.moveTo(x, y);
        fillPath.moveTo(x, size.height);
        fillPath.lineTo(x, y);
      } else {
        linePath.lineTo(x, y);
        fillPath.lineTo(x, y);
      }
    }
    fillPath.lineTo(size.width, size.height);
    fillPath.close();

    canvas.drawPath(
      fillPath,
      Paint()
        ..shader = LinearGradient(
          colors: [
            color.withValues(alpha: 0.22),
            color.withValues(alpha: 0.02),
          ],
          begin: Alignment.topCenter,
          end: Alignment.bottomCenter,
        ).createShader(Offset.zero & size),
    );
    canvas.drawPath(
      linePath,
      Paint()
        ..color = color
        ..style = PaintingStyle.stroke
        ..strokeWidth = 3
        ..strokeCap = StrokeCap.round
        ..strokeJoin = StrokeJoin.round,
    );
  }

  @override
  bool shouldRepaint(covariant _SparklinePainter oldDelegate) {
    return oldDelegate.series != series || oldDelegate.color != color;
  }
}

double? _asDouble(dynamic value) {
  if (value is num) {
    return value.toDouble();
  }
  return null;
}

double? _averageZoneValue(List<dynamic> zones, String key) {
  double total = 0;
  int count = 0;
  for (final zone in zones) {
    if (zone is! Map<String, dynamic>) {
      continue;
    }
    final value = _asDouble(zone[key]);
    if (value == null) {
      continue;
    }
    total += value;
    count++;
  }
  return count == 0 ? null : total / count;
}

double _phaseProgress(Map<String, dynamic> data) {
  final elapsed = _asDouble(data['phaseElapsedMs']) ?? 0;
  final duration = _asDouble(data['phaseDurationMs']) ?? 0;
  if (duration <= 0) {
    return 0;
  }
  return (elapsed / duration).clamp(0, 1);
}

String _phaseRemainingText(Map<String, dynamic> data) {
  final remainingMs = _asDouble(data['phaseRemainingMs']);
  if (remainingMs == null) {
    return '--';
  }
  final totalSeconds = (remainingMs / 1000).round();
  final minutes = totalSeconds ~/ 60;
  final seconds = totalSeconds % 60;
  return '${minutes.toString().padLeft(2, '0')}:${seconds.toString().padLeft(2, '0')}';
}

String _sampleAgeText(dynamic sampleAgeMs) {
  final age = _asDouble(sampleAgeMs);
  if (age == null) {
    return '--';
  }
  final seconds = (age / 1000).round();
  if (seconds < 60) {
    return '${seconds}s ago';
  }
  final minutes = seconds ~/ 60;
  return '${minutes}m ago';
}

double _freshnessValue(dynamic sampleAgeMs) {
  final age = _asDouble(sampleAgeMs);
  if (age == null) {
    return 0;
  }
  return (1 - (age / 300000)).clamp(0, 1);
}

double _tankPercent(double? tankDistance, bool isLow) {
  if (tankDistance == null) {
    return 0.25;
  }
  final fill = 1 - ((tankDistance - 4) / 24);
  final clamped = fill.clamp(0.08, 1.0);
  if (isLow) {
    return clamped.clamp(0.08, 0.28);
  }
  return clamped;
}

String _moistureBandLabel(double moisture) {
  if (moisture < 20) return 'Very dry';
  if (moisture < 30) return 'A little dry';
  if (moisture <= 45) return 'Good range';
  return 'Too wet';
}

String _nutrientBandLabel(double nutrient) {
  if (nutrient < 600) return 'Very low';
  if (nutrient < 800) return 'Low';
  if (nutrient <= 1200) return 'Good range';
  if (nutrient <= 1400) return 'A little high';
  return 'Too high';
}

List<double> _combineSeries(List<double> a, List<double> b) {
  final length = a.length > b.length ? a.length : b.length;
  final out = <double>[];
  for (int i = 0; i < length; i++) {
    final values = <double>[];
    if (i < a.length) values.add(a[i]);
    if (i < b.length) values.add(b[i]);
    if (values.isNotEmpty) {
      out.add(values.reduce((x, y) => x + y) / values.length);
    }
  }
  return out;
}

Color _bandColor(String band) {
  final normalized = band.toLowerCase();
  if (normalized.contains('good') || normalized.contains('optimal')) {
    return const Color(0xFF2F8F62);
  }
  if (normalized.contains('low') || normalized.contains('dry')) {
    return const Color(0xFFF59E0B);
  }
  if (normalized.contains('high') ||
      normalized.contains('unsafe') ||
      normalized.contains('wet')) {
    return const Color(0xFFDC2626);
  }
  return const Color(0xFF64748B);
}

class _DetailRow extends StatelessWidget {
  const _DetailRow({required this.label, required this.value});

  final String label;
  final String value;

  @override
  Widget build(BuildContext context) {
    return Padding(
      padding: const EdgeInsets.symmetric(vertical: 4),
      child: Row(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          Expanded(
            flex: 3,
            child: Text(
              label,
              style: const TextStyle(fontWeight: FontWeight.w600),
            ),
          ),
          const SizedBox(width: 8),
          Expanded(flex: 4, child: Text(value)),
        ],
      ),
    );
  }
}
