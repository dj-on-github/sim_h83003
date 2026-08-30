// No file system: the web build. There is nowhere to keep a config, so the
// app runs on its built-in defaults and saving says so.

import 'sim_config.dart';

String? simConfigPath() => null;

SimConfig? loadSimConfig() => null;

String saveSimConfig(SimConfig config) =>
    throw UnsupportedError('there is no home directory on this platform');
