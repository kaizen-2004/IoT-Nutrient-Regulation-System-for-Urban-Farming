import 'dart:async';
import 'dart:convert';
import 'dart:io';

import 'package:flutter/material.dart';
import 'package:flutter_local_notifications/flutter_local_notifications.dart';
import 'package:http/http.dart' as http;
import 'package:mobile_scanner/mobile_scanner.dart';
import 'package:shared_preferences/shared_preferences.dart';

Future<void> main() async {
  WidgetsFlutterBinding.ensureInitialized();
  await NotificationService.instance.initialize();
  runApp(const VertiFarmApp());
}

class NotificationService {
  NotificationService._();

  static final NotificationService instance = NotificationService._();
  static const _alertPrefix = 'notification_alert_';
  static const _tankCooldown = Duration(hours: 6);
  static const _offlineCooldown = Duration(minutes: 30);

  final FlutterLocalNotificationsPlugin _plugin =
      FlutterLocalNotificationsPlugin();

  bool _initialized = false;
  SharedPreferences? _prefs;

  Future<void> initialize() async {
    if (_initialized) {
      return;
    }

    const androidSettings = AndroidInitializationSettings(
      '@mipmap/ic_launcher',
    );
    const iosSettings = DarwinInitializationSettings();
    const settings = InitializationSettings(
      android: androidSettings,
      iOS: iosSettings,
    );

    await _plugin.initialize(settings);
    _prefs = await SharedPreferences.getInstance();
    await _plugin
        .resolvePlatformSpecificImplementation<
          AndroidFlutterLocalNotificationsPlugin
        >()
        ?.requestNotificationsPermission();
    await _plugin
        .resolvePlatformSpecificImplementation<
          IOSFlutterLocalNotificationsPlugin
        >()
        ?.requestPermissions(alert: true, badge: true, sound: true);

    const tankChannel = AndroidNotificationChannel(
      'nutrient_tank_alerts',
      'Tank Alerts',
      description: 'Alerts for low water tank conditions',
      importance: Importance.high,
    );
    const connectivityChannel = AndroidNotificationChannel(
      'nutrient_connectivity_alerts',
      'Connectivity Alerts',
      description: 'Alerts when the controller becomes unreachable',
      importance: Importance.defaultImportance,
    );
    final androidNotifications = _plugin
        .resolvePlatformSpecificImplementation<
          AndroidFlutterLocalNotificationsPlugin
        >();
    await androidNotifications?.createNotificationChannel(tankChannel);
    await androidNotifications?.createNotificationChannel(connectivityChannel);

    _initialized = true;
  }

  Future<void> showLowTankNotification({
    required String deviceName,
    required String body,
  }) async {
    if (!await _shouldNotify('$deviceName:tankLow', _tankCooldown)) {
      return;
    }

    const details = NotificationDetails(
      android: AndroidNotificationDetails(
        'nutrient_tank_alerts',
        'Tank Alerts',
        channelDescription: 'Alerts for low water tank conditions',
        importance: Importance.high,
        priority: Priority.high,
      ),
      iOS: DarwinNotificationDetails(),
    );

    await _plugin.show(1001, '$deviceName: Water tank low', body, details);
  }

  Future<void> showControllerOfflineNotification({
    required String deviceName,
    required String body,
  }) async {
    if (!await _shouldNotify('$deviceName:offline', _offlineCooldown)) {
      return;
    }

    const details = NotificationDetails(
      android: AndroidNotificationDetails(
        'nutrient_connectivity_alerts',
        'Connectivity Alerts',
        channelDescription: 'Alerts when the controller becomes unreachable',
        importance: Importance.defaultImportance,
        priority: Priority.defaultPriority,
      ),
      iOS: DarwinNotificationDetails(),
    );

    await _plugin.show(1002, '$deviceName: Controller offline', body, details);
  }

  Future<void> clearAlertState(String alertKey) async {
    await _prefs?.remove('$_alertPrefix$alertKey');
  }

  Future<bool> _shouldNotify(String alertKey, Duration cooldown) async {
    final prefs = _prefs;
    if (prefs == null) {
      return true;
    }

    final key = '$_alertPrefix$alertKey';
    final now = DateTime.now().millisecondsSinceEpoch;
    final last = prefs.getInt(key);
    if (last != null && now - last < cooldown.inMilliseconds) {
      return false;
    }

    await prefs.setInt(key, now);
    return true;
  }
}

class VertiFarmApp extends StatefulWidget {
  const VertiFarmApp({super.key});

  @override
  State<VertiFarmApp> createState() => _VertiFarmAppState();
}

class _VertiFarmAppState extends State<VertiFarmApp> {
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
      title: 'VertiFarm',
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
    return Scaffold(
      body: Center(
        child: Column(
          mainAxisSize: MainAxisSize.min,
          children: const [
            Icon(Icons.spa, size: 54, color: Color(0xFF2B7A4B)),
            SizedBox(height: 12),
            Text(
              'VertiFarm',
              style: TextStyle(
                fontSize: 24,
                fontWeight: FontWeight.w700,
                color: Color(0xFF173A2E),
              ),
            ),
            SizedBox(height: 16),
            CircularProgressIndicator(),
          ],
        ),
      ),
    );
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
      deviceName: (json['deviceName'] ?? 'VertiFarm Controller') as String,
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
        (body['error'] ?? body['reason'] ?? body['message'] ?? 'Request failed')
            as String,
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

  Future<Map<String, dynamic>> triggerManualPump({
    required String host,
    required String pumpId,
    int? durationMs,
  }) {
    final body = <String, dynamic>{'pumpId': pumpId};
    if (durationMs != null) {
      body['durationMs'] = durationMs;
    }
    return _postJson(host, '/api/manual/pump', body);
  }

  Future<Map<String, dynamic>> resetWifi(String host) {
    return _postJson(host, '/api/device/reset-wifi', {'confirm': true});
  }
}

Future<String?> discoverDeviceIp({
  required DeviceApi api,
  required String deviceId,
  String? preferredIp,
}) async {
  final prefixes = <String>[];

  if (preferredIp != null && preferredIp.isNotEmpty) {
    final lastDot = preferredIp.lastIndexOf('.');
    if (lastDot > 0) {
      prefixes.add(preferredIp.substring(0, lastDot + 1));
    }
  }

  for (final prefix in const ['192.168.1.', '192.168.0.', '10.0.0.']) {
    if (!prefixes.contains(prefix)) {
      prefixes.add(prefix);
    }
  }

  for (final prefix in prefixes) {
    final found = await _scanPrefixForDevice(
      api: api,
      prefix: prefix,
      expectedDeviceId: deviceId,
    );
    if (found != null) {
      return found;
    }
  }

  return null;
}

Future<String?> _scanPrefixForDevice({
  required DeviceApi api,
  required String prefix,
  required String expectedDeviceId,
}) async {
  const batchSize = 20;
  for (int start = 1; start <= 254; start += batchSize) {
    final futures = <Future<String?>>[];
    for (
      int offset = 0;
      offset < batchSize && start + offset <= 254;
      offset++
    ) {
      final ip = '$prefix${start + offset}';
      futures.add(
        _probeDeviceIp(api: api, ip: ip, expectedDeviceId: expectedDeviceId),
      );
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

Future<String?> _probeDeviceIp({
  required DeviceApi api,
  required String ip,
  required String expectedDeviceId,
}) async {
  try {
    final info = await api
        .fetchInfo(ip)
        .timeout(const Duration(milliseconds: 900));
    final foundDeviceId = (info['deviceId'] ?? '') as String;
    if (foundDeviceId == expectedDeviceId) {
      return ip;
    }
  } catch (_) {
    return null;
  }
  return null;
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
      appBar: AppBar(title: const Text('Add Controller')),
      body: ListView(
        padding: const EdgeInsets.all(20),
        children: [
          _HeroCard(
            title: 'Quick device setup',
            subtitle:
                'Scan the printed QR sticker, join the controller setup Wi-Fi, then send your home Wi-Fi details.',
            icon: Icons.qr_code_2,
          ),
          const SizedBox(height: 16),
          _SectionCard(
            child: Column(
              crossAxisAlignment: CrossAxisAlignment.start,
              children: const [
                _SectionHeader(
                  title: 'Before you start',
                  subtitle:
                      'Use a 2.4 GHz Wi-Fi network. Keep the controller powered on and close to your phone during setup.',
                ),
                SizedBox(height: 14),
                _OnboardingStep(
                  number: '1',
                  title: 'Scan the QR sticker',
                  body:
                      'The sticker provides the setup AP name and device identity.',
                ),
                SizedBox(height: 10),
                _OnboardingStep(
                  number: '2',
                  title: 'Join the setup Wi-Fi',
                  body:
                      'Connect your phone to the temporary NutrientReg-Setup network when asked.',
                ),
                SizedBox(height: 10),
                _OnboardingStep(
                  number: '3',
                  title: 'Send home Wi-Fi',
                  body:
                      'Enter your router name and password so the controller can move onto your normal network.',
                ),
              ],
            ),
          ),
          const SizedBox(height: 16),
          Card(
            child: Padding(
              padding: const EdgeInsets.all(16),
              child: Column(
                crossAxisAlignment: CrossAxisAlignment.start,
                children: [
                  Text(
                    'Start onboarding',
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
            _SectionCard(
              child: Column(
                crossAxisAlignment: CrossAxisAlignment.start,
                children: [
                  const _SectionHeader(
                    title: 'Controller found',
                    subtitle:
                        'The QR code looks valid. Continue to the Wi-Fi handoff step.',
                  ),
                  const SizedBox(height: 12),
                  _DetailRow(label: 'Model', value: _payload!.model),
                  _DetailRow(label: 'Device ID', value: _payload!.deviceId),
                  _DetailRow(label: 'Setup AP', value: _payload!.setupAp),
                  _DetailRow(label: 'Setup IP', value: _payload!.setupIp),
                  const SizedBox(height: 16),
                  FilledButton(
                    onPressed: _continueProvisioning,
                    child: const Text('Continue to Wi-Fi setup'),
                  ),
                ],
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
            'Could not reach the controller setup Wi-Fi. Join the controller AP on your phone first. If the AP name differs from the QR sticker, use the AP shown in your Wi-Fi list or on the controller LCD.';
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
      final detectedIp = await discoverDeviceIp(
        api: _api,
        deviceId: widget.payload.deviceId,
        preferredIp: _deviceIpController.text.trim(),
      );
      if (!mounted || detectedIp == null) {
        return;
      }

      _deviceIpController.text = detectedIp;
      final info = await _api.fetchInfo(detectedIp);
      final device = DeviceRecord(
        deviceId: (info['deviceId'] ?? widget.payload.deviceId) as String,
        deviceName: (info['deviceName'] ?? 'VertiFarm Controller') as String,
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
          deviceName: (info['deviceName'] ?? 'VertiFarm Controller') as String,
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
        deviceName: (info['deviceName'] ?? 'VertiFarm Controller') as String,
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
          'Two options are available here: the app can auto-detect the controller on your home Wi-Fi, or you can read the IP from the controller LCD and type it manually.',
        ),
        const SizedBox(height: 12),
        const Text(
          'Option 1: Auto-detect (recommended)',
          style: TextStyle(fontWeight: FontWeight.w700),
        ),
        const SizedBox(height: 4),
        Text(
          _discoveringLanIp
              ? 'Scanning your local network for this controller now...'
              : 'After the setup AP handoff, the app will try the saved subnet and common home-network ranges automatically.',
          style: const TextStyle(color: Color(0xFF5E6B63)),
        ),
        const SizedBox(height: 12),
        const Text(
          'Option 2: Enter IP from LCD',
          style: TextStyle(fontWeight: FontWeight.w700),
        ),
        const SizedBox(height: 4),
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
      ],
    );
  }

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      appBar: AppBar(title: const Text('Wi-Fi Setup')),
      body: ListView(
        padding: const EdgeInsets.all(20),
        children: [
          _HeroCard(
            title: 'Join the controller Wi-Fi first',
            subtitle:
                'In your phone Wi-Fi settings, connect to ${widget.payload.setupAp}. Then come back here and continue the handoff.',
            icon: Icons.wifi,
          ),
          const SizedBox(height: 16),
          _SectionCard(
            child: Padding(
              padding: const EdgeInsets.all(4),
              child: Form(
                key: _formKey,
                child: Column(
                  crossAxisAlignment: CrossAxisAlignment.start,
                  children: [
                    const _SectionHeader(
                      title: 'Step 1: Confirm setup Wi-Fi',
                      subtitle:
                          'Check that your phone can still reach the temporary controller network before sending credentials.',
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
                      label: const Text('Check controller setup Wi-Fi'),
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
                    const _SectionHeader(
                      title: 'Step 2: Send home Wi-Fi',
                      subtitle:
                          'Use the same 2.4 GHz network your controller should join after setup.',
                    ),
                    const SizedBox(height: 12),
                    TextFormField(
                      controller: _ssidController,
                      decoration: const InputDecoration(
                        labelText: 'Home Wi-Fi name',
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
                        labelText: 'Home Wi-Fi password',
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
                      label: const Text('Connect controller to home Wi-Fi'),
                    ),
                    const SizedBox(height: 12),
                    Container(
                      width: double.infinity,
                      padding: const EdgeInsets.all(12),
                      decoration: BoxDecoration(
                        color: const Color(0xFFF5F8F4),
                        borderRadius: BorderRadius.circular(16),
                      ),
                      child: Text(_status),
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

class _DashboardScreenState extends State<DashboardScreen>
    with WidgetsBindingObserver {
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
  bool _lowTankNotificationShown = false;
  bool _offlineNotificationShown = false;
  bool _discoveringDevice = false;
  DateTime? _lastDiscoveryAttemptAt;
  final Set<String> _manualPendingPumpIds = <String>{};

  String get _host => widget.device.lastKnownIp;

  @override
  void initState() {
    super.initState();
    WidgetsBinding.instance.addObserver(this);
    _refresh();
    _pollTimer = Timer.periodic(const Duration(seconds: 3), (_) {
      _refresh(silent: true);
    });
  }

  @override
  void dispose() {
    WidgetsBinding.instance.removeObserver(this);
    _pollTimer?.cancel();
    super.dispose();
  }

  @override
  void didChangeAppLifecycleState(AppLifecycleState state) {
    if (state == AppLifecycleState.resumed) {
      _refresh();
    }
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
      await _handleStatusNotifications(status, updated.deviceName);
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
      await _handleOfflineNotification();
      _scheduleDiscoveryIfNeeded();
      if (!mounted) {
        return;
      }
      setState(() {
        _error =
            'Controller unreachable at ${widget.device.lastKnownIp}. Reconnect your phone to the same Wi-Fi network as the controller, then try again.';
      });
    } finally {
      if (mounted) {
        setState(() {
          _busy = false;
        });
      }
    }
  }

  void _scheduleDiscoveryIfNeeded() {
    final now = DateTime.now();
    if (_discoveringDevice) {
      return;
    }
    if (_lastDiscoveryAttemptAt != null &&
        now.difference(_lastDiscoveryAttemptAt!) <
            const Duration(seconds: 20)) {
      return;
    }
    _lastDiscoveryAttemptAt = now;
    unawaited(_rediscoverDeviceIp());
  }

  Future<void> _rediscoverDeviceIp() async {
    _discoveringDevice = true;
    try {
      final detectedIp = await discoverDeviceIp(
        api: _api,
        deviceId: widget.device.deviceId,
        preferredIp: widget.device.lastKnownIp,
      );
      if (detectedIp == null) {
        return;
      }

      final info = await _api.fetchInfo(detectedIp);
      final status = await _api.fetchStatus(detectedIp);
      final updated = widget.device.copyWith(
        deviceName: (info['deviceName'] ?? widget.device.deviceName) as String,
        lastKnownIp: detectedIp,
      );
      await widget.onDeviceUpdated(updated);
      await _handleStatusNotifications(status, updated.deviceName);

      if (!mounted) {
        return;
      }

      _recordHistory(status);
      setState(() {
        _info = info;
        _status = status;
        _error = null;
      });
    } catch (_) {
      return;
    } finally {
      _discoveringDevice = false;
    }
  }

  Future<void> _handleStatusNotifications(
    Map<String, dynamic> status,
    String deviceName,
  ) async {
    final tankLow = (status['tankLow'] ?? false) == true;
    _offlineNotificationShown = false;
    await NotificationService.instance.clearAlertState('$deviceName:offline');

    if (tankLow && !_lowTankNotificationShown) {
      _lowTankNotificationShown = true;
      await NotificationService.instance.showLowTankNotification(
        deviceName: deviceName,
        body:
            'The controller reported a low water tank. Refill the tank soon to avoid watering interruption.',
      );
    } else if (!tankLow) {
      _lowTankNotificationShown = false;
      await NotificationService.instance.clearAlertState('$deviceName:tankLow');
    }
  }

  Future<void> _handleOfflineNotification() async {
    if (_offlineNotificationShown) {
      return;
    }

    _offlineNotificationShown = true;
    await NotificationService.instance.showControllerOfflineNotification(
      deviceName: widget.device.deviceName,
      body:
          'The app could not reach the controller at ${widget.device.lastKnownIp}. Check power and make sure your phone is on the same Wi-Fi network.',
    );
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

  Future<void> _triggerManualPump(String pumpId) async {
    if (_manualPendingPumpIds.contains(pumpId)) {
      return;
    }

    if (mounted) {
      setState(() {
        _manualPendingPumpIds.add(pumpId);
      });
    }

    try {
      final response = await _api.triggerManualPump(
        host: _host,
        pumpId: pumpId,
        durationMs: 6000,
      );
      if (!mounted) {
        return;
      }

      final durationMs = response['durationMs'];
      final durationText = durationMs is num
          ? '${(durationMs / 1000).toStringAsFixed(1)}s'
          : 'default duration';
      ScaffoldMessenger.of(context).showSnackBar(
        SnackBar(
          content: Text('Manual pump $pumpId started for $durationText.'),
        ),
      );
      await _refresh(silent: true);
    } catch (error) {
      if (!mounted) {
        return;
      }
      ScaffoldMessenger.of(context).showSnackBar(
        SnackBar(
          content: Text(error.toString().replaceFirst('Exception: ', '')),
        ),
      );
    } finally {
      if (mounted) {
        setState(() {
          _manualPendingPumpIds.remove(pumpId);
        });
      }
    }
  }

  @override
  Widget build(BuildContext context) {
    final status = _status;
    final wifi =
        (status?['wifi'] as Map<String, dynamic>?) ?? const <String, dynamic>{};
    final zones = (status?['zones'] as List<dynamic>?) ?? const <dynamic>[];
    final hasConnectionError = _error != null;
    final isOnline =
        !hasConnectionError && (wifi['connected'] ?? false) == true;

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
                title: Text(
                  isOnline ? 'Controller online' : 'Controller offline',
                ),
                subtitle: Text(
                  _error ??
                      (isOnline
                          ? 'Refreshing every 3 seconds'
                          : (_discoveringDevice
                                ? 'Trying to rediscover the controller on your local network...'
                                : 'Reconnect your phone to the same Wi-Fi as the controller, then refresh.')),
                ),
              ),
            ),
            Expanded(
              child: TabBarView(
                children: [
                  _OverviewTab(
                    status: status,
                    histories: _zoneHistory,
                    onManualPumpTrigger: _triggerManualPump,
                    manualPendingPumpIds: _manualPendingPumpIds,
                  ),
                  _ZonesTab(zones: zones, histories: _zoneHistory),
                  _DeviceTab(
                    device: widget.device,
                    info: _info,
                    status: status,
                    onResetWifi: _resetWifi,
                    onManualPumpTrigger: _triggerManualPump,
                    manualPendingPumpIds: _manualPendingPumpIds,
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
  const _OverviewTab({
    required this.status,
    required this.histories,
    required this.onManualPumpTrigger,
    required this.manualPendingPumpIds,
  });

  final Map<String, dynamic>? status;
  final Map<int, _ZoneHistory> histories;
  final Future<void> Function(String pumpId) onManualPumpTrigger;
  final Set<String> manualPendingPumpIds;

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
    final manual =
        (data['manual'] as Map<String, dynamic>?) ?? const <String, dynamic>{};
    final manualPumps =
        (manual['pumps'] as List<dynamic>?) ?? const <dynamic>[];

    Map<String, dynamic>? manualPumpById(String id) {
      for (final entry in manualPumps) {
        if (entry is! Map<String, dynamic>) {
          continue;
        }
        if ('${entry['pumpId'] ?? ''}' == id) {
          return entry;
        }
      }
      return null;
    }

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
                'VertiFarm automated monitoring and nutrient control system',
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
        _SectionCard(
          child: Column(
            crossAxisAlignment: CrossAxisAlignment.start,
            children: [
              const _SectionHeader(
                title: 'Readings',
                subtitle: 'Current combined sensor summary from the controller',
              ),
              const SizedBox(height: 16),
              Row(
                children: [
                  Expanded(
                    child: _SpotlightCard(
                      title: 'Avg Soil Wetness',
                      value: avgMoisture == null
                          ? '--'
                          : '${avgMoisture.round()}%',
                      subtitle: avgMoisture == null
                          ? 'Waiting'
                          : _moistureBandLabel(avgMoisture),
                      progress: avgMoisture == null
                          ? 0
                          : (avgMoisture / 100).clamp(0, 1).toDouble(),
                      accentColor: const Color(0xFF2F8F62),
                    ),
                  ),
                  const SizedBox(width: 8),
                  Expanded(
                    child: _SummaryStatCard(
                      title: 'Plant Food',
                      value: avgNutrient == null
                          ? '--'
                          : '${avgNutrient.round()} ppm',
                      subtitle: avgNutrient == null
                          ? 'No sample'
                          : _nutrientBandLabel(avgNutrient),
                    ),
                  ),
                  const SizedBox(width: 8),
                  Expanded(
                    child: _SummaryStatCard(
                      title: 'Time Left',
                      value: _phaseRemainingText(data),
                      subtitle: 'Current phase',
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
                      subtitle: 'Channel 1',
                    ),
                  ),
                  const SizedBox(width: 12),
                  Expanded(
                    child: _SummaryStatCard(
                      title: 'Nutrient valve',
                      value: (data['nutrientValveOpen'] ?? false) == true
                          ? 'Open'
                          : 'Closed',
                      subtitle: 'Channel 2',
                    ),
                  ),
                ],
              ),
              const SizedBox(height: 12),
              const _SectionHeader(
                title: 'Manual watering',
                subtitle: 'Tap a pump to run a timed watering pulse',
              ),
              const SizedBox(height: 12),
              LayoutBuilder(
                builder: (context, constraints) {
                  final twoColumns = constraints.maxWidth >= 460;
                  final children = [
                    _ManualPumpCard(
                      title: 'Zone 1 Pump A',
                      pumpId: 'z1a',
                      pumpState: manualPumpById('z1a'),
                      isPending: manualPendingPumpIds.contains('z1a'),
                      onTrigger: onManualPumpTrigger,
                    ),
                    _ManualPumpCard(
                      title: 'Zone 1 Pump B',
                      pumpId: 'z1b',
                      pumpState: manualPumpById('z1b'),
                      isPending: manualPendingPumpIds.contains('z1b'),
                      onTrigger: onManualPumpTrigger,
                    ),
                    _ManualPumpCard(
                      title: 'Zone 2 Pump A',
                      pumpId: 'z2a',
                      pumpState: manualPumpById('z2a'),
                      isPending: manualPendingPumpIds.contains('z2a'),
                      onTrigger: onManualPumpTrigger,
                    ),
                    _ManualPumpCard(
                      title: 'Zone 2 Pump B',
                      pumpId: 'z2b',
                      pumpState: manualPumpById('z2b'),
                      isPending: manualPendingPumpIds.contains('z2b'),
                      onTrigger: onManualPumpTrigger,
                    ),
                  ];

                  if (!twoColumns) {
                    return Column(
                      children: [
                        for (int i = 0; i < children.length; i++) ...[
                          children[i],
                          if (i < children.length - 1)
                            const SizedBox(height: 10),
                        ],
                      ],
                    );
                  }

                  return Column(
                    children: [
                      Row(
                        children: [
                          Expanded(child: children[0]),
                          const SizedBox(width: 10),
                          Expanded(child: children[1]),
                        ],
                      ),
                      const SizedBox(height: 10),
                      Row(
                        children: [
                          Expanded(child: children[2]),
                          const SizedBox(width: 10),
                          Expanded(child: children[3]),
                        ],
                      ),
                    ],
                  );
                },
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
    required this.onManualPumpTrigger,
    required this.manualPendingPumpIds,
  });

  final DeviceRecord device;
  final Map<String, dynamic>? info;
  final Map<String, dynamic>? status;
  final Future<void> Function() onResetWifi;
  final Future<void> Function(String pumpId) onManualPumpTrigger;
  final Set<String> manualPendingPumpIds;

  @override
  Widget build(BuildContext context) {
    final wifi =
        (status?['wifi'] as Map<String, dynamic>?) ?? const <String, dynamic>{};
    final telemetrySource = '${status?['telemetrySource'] ?? 'esp32_local'}';
    final unoTelemetryAgeMs =
        (status?['unoTelemetryAgeMs'] as num?)?.toInt() ?? 0;
    final unoTelemetrySeq = (status?['unoTelemetrySeq'] as num?)?.toInt() ?? 0;
    final unoTelemetryFresh = (status?['unoTelemetryFresh'] ?? false) == true;
    final manual =
        (status?['manual'] as Map<String, dynamic>?) ??
        const <String, dynamic>{};
    final manualPumps =
        (manual['pumps'] as List<dynamic>?) ?? const <dynamic>[];

    Map<String, dynamic>? manualPumpById(String id) {
      for (final entry in manualPumps) {
        if (entry is! Map<String, dynamic>) {
          continue;
        }
        if ('${entry['pumpId'] ?? ''}' == id) {
          return entry;
        }
      }
      return null;
    }

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
          child: Padding(
            padding: const EdgeInsets.all(4),
            child: Column(
              crossAxisAlignment: CrossAxisAlignment.start,
              children: [
                const _SectionHeader(
                  title: 'Serial link diagnostics',
                  subtitle: 'UNO telemetry freshness and link health',
                ),
                const SizedBox(height: 12),
                _DetailRow(label: 'Telemetry source', value: telemetrySource),
                _DetailRow(
                  label: 'Uno telemetry fresh',
                  value: unoTelemetryFresh ? 'Yes' : 'No',
                ),
                _DetailRow(
                  label: 'Uno telemetry age',
                  value: '${(unoTelemetryAgeMs / 1000).toStringAsFixed(1)} s',
                ),
                _DetailRow(label: 'Uno frame seq', value: '$unoTelemetrySeq'),
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
                title: 'Manual control',
                subtitle: 'Timed manual pump pulses from the mobile app',
              ),
              const SizedBox(height: 12),
              LayoutBuilder(
                builder: (context, constraints) {
                  final twoColumns = constraints.maxWidth >= 460;
                  final children = [
                    _ManualPumpCard(
                      title: 'Zone 1 Pump A',
                      pumpId: 'z1a',
                      pumpState: manualPumpById('z1a'),
                      isPending: manualPendingPumpIds.contains('z1a'),
                      onTrigger: onManualPumpTrigger,
                    ),
                    _ManualPumpCard(
                      title: 'Zone 1 Pump B',
                      pumpId: 'z1b',
                      pumpState: manualPumpById('z1b'),
                      isPending: manualPendingPumpIds.contains('z1b'),
                      onTrigger: onManualPumpTrigger,
                    ),
                    _ManualPumpCard(
                      title: 'Zone 2 Pump A',
                      pumpId: 'z2a',
                      pumpState: manualPumpById('z2a'),
                      isPending: manualPendingPumpIds.contains('z2a'),
                      onTrigger: onManualPumpTrigger,
                    ),
                    _ManualPumpCard(
                      title: 'Zone 2 Pump B',
                      pumpId: 'z2b',
                      pumpState: manualPumpById('z2b'),
                      isPending: manualPendingPumpIds.contains('z2b'),
                      onTrigger: onManualPumpTrigger,
                    ),
                  ];

                  if (!twoColumns) {
                    return Column(
                      children: [
                        for (int i = 0; i < children.length; i++) ...[
                          children[i],
                          if (i < children.length - 1)
                            const SizedBox(height: 10),
                        ],
                      ],
                    );
                  }

                  return Column(
                    children: [
                      Row(
                        children: [
                          Expanded(child: children[0]),
                          const SizedBox(width: 10),
                          Expanded(child: children[1]),
                        ],
                      ),
                      const SizedBox(height: 10),
                      Row(
                        children: [
                          Expanded(child: children[2]),
                          const SizedBox(width: 10),
                          Expanded(child: children[3]),
                        ],
                      ),
                    ],
                  );
                },
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

class _OnboardingStep extends StatelessWidget {
  const _OnboardingStep({
    required this.number,
    required this.title,
    required this.body,
  });

  final String number;
  final String title;
  final String body;

  @override
  Widget build(BuildContext context) {
    return Row(
      crossAxisAlignment: CrossAxisAlignment.start,
      children: [
        Container(
          width: 32,
          height: 32,
          decoration: const BoxDecoration(
            color: Color(0xFF1F5A39),
            shape: BoxShape.circle,
          ),
          alignment: Alignment.center,
          child: Text(
            number,
            style: const TextStyle(
              color: Colors.white,
              fontWeight: FontWeight.w700,
            ),
          ),
        ),
        const SizedBox(width: 12),
        Expanded(
          child: Column(
            crossAxisAlignment: CrossAxisAlignment.start,
            children: [
              Text(
                title,
                style: Theme.of(
                  context,
                ).textTheme.titleSmall?.copyWith(fontWeight: FontWeight.w700),
              ),
              const SizedBox(height: 4),
              Text(body, style: const TextStyle(color: Color(0xFF5E6B63))),
            ],
          ),
        ),
      ],
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
      padding: const EdgeInsets.all(12),
      child: Column(
        mainAxisSize: MainAxisSize.min,
        children: [
          Text(
            title,
            textAlign: TextAlign.center,
            style: Theme.of(
              context,
            ).textTheme.bodySmall?.copyWith(fontWeight: FontWeight.w700),
          ),
          const SizedBox(height: 6),
          Text(
            value,
            textAlign: TextAlign.center,
            style: Theme.of(context).textTheme.titleLarge?.copyWith(
              fontWeight: FontWeight.w800,
              height: 1.0,
            ),
          ),
          const SizedBox(height: 8),
          SizedBox(
            width: 60,
            height: 60,
            child: Stack(
              alignment: Alignment.center,
              children: [
                CircularProgressIndicator(
                  value: progress.clamp(0, 1),
                  strokeWidth: 6,
                  backgroundColor: const Color(0xFFE4ECE6),
                  valueColor: AlwaysStoppedAnimation<Color>(accentColor),
                ),
              ],
            ),
          ),
          const SizedBox(height: 6),
          Text(
            subtitle,
            textAlign: TextAlign.center,
            style: const TextStyle(color: Color(0xFF5E6B63), fontSize: 10),
            maxLines: 1,
            overflow: TextOverflow.ellipsis,
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

class _ManualPumpCard extends StatelessWidget {
  const _ManualPumpCard({
    required this.title,
    required this.pumpId,
    required this.pumpState,
    required this.isPending,
    required this.onTrigger,
  });

  final String title;
  final String pumpId;
  final Map<String, dynamic>? pumpState;
  final bool isPending;
  final Future<void> Function(String pumpId) onTrigger;

  @override
  Widget build(BuildContext context) {
    final configured = (pumpState?['configured'] ?? false) == true;
    final active = (pumpState?['active'] ?? false) == true;
    final runningRemainingMs =
        ((pumpState?['runningRemainingMs'] as num?)?.toInt() ?? 0).clamp(
          0,
          999999,
        );
    final cooldownRemainingMs =
        ((pumpState?['cooldownRemainingMs'] as num?)?.toInt() ?? 0).clamp(
          0,
          999999,
        );

    final cooldownSeconds = (cooldownRemainingMs / 1000).ceil();
    final runningSeconds = (runningRemainingMs / 1000).ceil();

    String status = 'Ready';
    if (!configured) {
      status = 'Not wired';
    } else if (isPending) {
      status = 'Starting...';
    } else if (active) {
      status = 'Running ${runningSeconds}s';
    } else if (cooldownRemainingMs > 0) {
      status = 'Cooldown ${cooldownSeconds}s';
    }

    final canRun =
        configured && !active && cooldownRemainingMs == 0 && !isPending;

    return _SectionCard(
      padding: const EdgeInsets.all(12),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          Text(title, style: const TextStyle(color: Color(0xFF5E6B63))),
          const SizedBox(height: 8),
          Text(
            status,
            style: Theme.of(
              context,
            ).textTheme.titleMedium?.copyWith(fontWeight: FontWeight.w700),
          ),
          const SizedBox(height: 8),
          SizedBox(
            width: double.infinity,
            child: FilledButton.tonal(
              onPressed: canRun ? () => unawaited(onTrigger(pumpId)) : null,
              child: Text(canRun ? 'Run 6s pulse' : 'Unavailable'),
            ),
          ),
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
