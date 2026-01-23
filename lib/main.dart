import 'dart:async';
import 'dart:io';

import 'package:agus_maps_flutter/agus_maps_flutter.dart'
    as agus_maps_flutter;
import 'package:flutter/material.dart';
import 'package:flutter/scheduler.dart';

void main() {
  WidgetsFlutterBinding.ensureInitialized();
  runApp(const MyApp());
}

/// Default focus: Philippines center.
const double kFocusLat = 13.0;
const double kFocusLon = 122.0;
const int kFocusZoom = 4;

class MyApp extends StatefulWidget {
  const MyApp({super.key});

  @override
  State<MyApp> createState() => _MyAppState();
}

class _MyAppState extends State<MyApp> {
  final agus_maps_flutter.AgusMapController _mapController =
      agus_maps_flutter.AgusMapController();

  String _status = 'Initializing...';
  String _debug = '';
  bool _dataReady = false;

  final List<String> _mapPathsToRegister = [];
  int? _bundledMwmVersion;

  @override
  void initState() {
    super.initState();
    SchedulerBinding.instance.addPostFrameCallback((_) {
      _initData();
    });
  }

  void _log(String msg) {
    debugPrint('[AgusExample] $msg');
    if (!mounted) return;
    setState(() {
      _debug += '$msg\n';
    });
  }

  Future<void> _initData() async {
    try {
      _log('Starting initialization...');

      final maps = [
        'World.mwm',
        'WorldCoasts.mwm',
        'Philippines_Luzon_Manila.mwm',
        'Philippines_Luzon_North.mwm',
        'Philippines_Luzon_South.mwm',
        'Philippines_Mindanao.mwm',
        'Philippines_Visayas.mwm',
      ];

      for (final map in maps) {
        _log('Extracting $map...');
        final path = await agus_maps_flutter.extractMap('assets/maps/$map');
        _mapPathsToRegister.add(path);
      }

      _log('Extracting icudt75l.dat...');
      await agus_maps_flutter.extractMap('assets/maps/icudt75l.dat');

      _log('Extracting data files...');
      final dataPath = await agus_maps_flutter.extractDataFiles();
      _log('Data path: $dataPath');

      _bundledMwmVersion = await _readBundledMwmVersion(dataPath);
      _log('Bundled MWM version: ${_bundledMwmVersion ?? 'unknown'}');

      _log('Calling initWithPaths()...');
      agus_maps_flutter.initWithPaths(dataPath, dataPath);
      _log('initWithPaths() complete');

      if (!mounted) return;
      setState(() {
        _status = 'Data ready - creating map...';
        _dataReady = true;
      });
    } catch (e, stackTrace) {
      _log('ERROR: $e\n$stackTrace');
      if (!mounted) return;
      setState(() {
        _status = 'Error: $e';
      });
    }
  }

  Future<int?> _readBundledMwmVersion(String dataPath) async {
    try {
      final file = File('$dataPath/countries.txt');
      if (!await file.exists()) {
        return null;
      }
      final contents = await file.readAsString();
      final match = RegExp(r'"v"\s*:\s*(\d+)').firstMatch(contents);
      if (match != null) {
        return int.tryParse(match.group(1)!);
      }
      return null;
    } catch (_) {
      return null;
    }
  }

  void _onMapReady() {
    unawaited(_onMapReadyAsync());
  }

  Future<void> _onMapReadyAsync() async {
    _log('Map surface ready! Registering maps...');

    final bundledVersion = _bundledMwmVersion;
    if (bundledVersion == null) {
      _log('WARNING: bundled MWM version unknown; registrations may fail.');
    }

    for (final path in _mapPathsToRegister) {
      final result = bundledVersion != null
          ? agus_maps_flutter.registerSingleMapWithVersion(path, bundledVersion)
          : agus_maps_flutter.registerSingleMap(path);
      _log('Registered $path: result=$result');

      if (result == 2) {
        final fileName = File(path).uri.pathSegments.last;
        final assetPath = 'assets/maps/$fileName';
        _log('$fileName is too old; re-extracting from $assetPath...');
        try {
          final f = File(path);
          if (await f.exists()) {
            await f.delete();
          }
          final newPath = await agus_maps_flutter.extractMap(assetPath);
          final retry = bundledVersion != null
              ? agus_maps_flutter.registerSingleMapWithVersion(
                  newPath,
                  bundledVersion,
                )
              : agus_maps_flutter.registerSingleMap(newPath);
          _log('Re-registered $newPath: result=$retry');
        } catch (e, st) {
          _log('Failed to re-extract/re-register $fileName: $e\n$st');
        }
      }
    }

    _log('Invalidating map viewport...');
    agus_maps_flutter.invalidateMap();

    _log('Forcing complete tile reload...');
    agus_maps_flutter.forceRedraw();

    await Future.delayed(const Duration(milliseconds: 1500));
    _log('Moving to Philippines...');
    _mapController.moveToLocation(kFocusLat, kFocusLon, kFocusZoom);

    if (!mounted) return;
    setState(() {
      _status = 'Map ready!';
    });
  }

  @override
  Widget build(BuildContext context) {
    return MaterialApp(
      debugShowCheckedModeBanner: false,
      home: Scaffold(
        body: _buildBody(),
      ),
    );
  }

  Widget _buildBody() {
    if (!_dataReady) {
      return Container(
        color: Colors.black,
        child: Center(
          child: Column(
            mainAxisSize: MainAxisSize.min,
            children: [
              const CircularProgressIndicator(),
              const SizedBox(height: 16),
              Text(
                _status,
                style: const TextStyle(color: Colors.white),
              ),
              const SizedBox(height: 16),
              SizedBox(
                height: 160,
                child: SingleChildScrollView(
                  padding: const EdgeInsets.symmetric(horizontal: 16),
                  child: Text(
                    _debug,
                    style: const TextStyle(
                      fontSize: 10,
                      fontFamily: 'monospace',
                      color: Colors.white70,
                    ),
                  ),
                ),
              ),
            ],
          ),
        ),
      );
    }

    return SizedBox.expand(
      child: agus_maps_flutter.AgusMap(
        onMapReady: _onMapReady,
        controller: _mapController,
      ),
    );
  }
}
