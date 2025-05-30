#!/usr/bin/env python
import pathlib
from unittest import mock

import mozunit
import pytest

from mozperftest.environment import SYSTEM
from mozperftest.system.android import AndroidSetupError, DeviceError
from mozperftest.system.android_perf_tuner import PerformanceTuner
from mozperftest.tests.support import (
    FakeDevice,
    get_running_env,
    requests_content,
    temp_file,
)
from mozperftest.utils import silence


def get_android_device_layer(layers):
    for layer in layers:
        if layer.__class__.__name__ == "AndroidDevice":
            return layer
    return None


@mock.patch("mozperftest.system.VersionProducer", new=mock.MagicMock())
@mock.patch("mozperftest.system.android.ADBLoggedDevice", new=FakeDevice)
def test_android():
    args = {
        "app": "fenix",
        "flavor": "mobile-browser",
        "android-install-apk": ["this.apk"],
        "android": True,
        "android-timeout": 30,
        "android-capture-adb": "stdout",
        "android-app-name": "org.mozilla.fenix",
    }

    mach_cmd, metadata, env = get_running_env(**args)
    system = env.layers[SYSTEM]
    with system as android, silence(system):
        android(metadata)


@mock.patch("mozperftest.system.VersionProducer", new=mock.MagicMock())
@mock.patch("mozperftest.system.android.ADBLoggedDevice", new=FakeDevice)
def test_android_with_binary():
    args = {
        "flavor": "mobile-browser",
        "android-install-apk": ["this.apk"],
        "android": True,
        "android-timeout": 30,
        "android-capture-adb": "stdout",
        "app": "fenix",
        "binary": "org.mozilla.fenix-fake",
    }

    mach_cmd, metadata, env = get_running_env(**args)
    system = env.layers[SYSTEM]
    with system as android, silence(system):
        android(metadata)

    android_layer = get_android_device_layer(system.layers)
    assert android_layer.app_name == "org.mozilla.fenix-fake"


@mock.patch("mozperftest.system.VersionProducer", new=mock.MagicMock())
@mock.patch("mozperftest.system.android.ADBLoggedDevice", new=FakeDevice)
def test_android_with_bad_app():
    args = {
        "flavor": "mobile-browser",
        "android": True,
        "app": "firefox",
    }

    mach_cmd, metadata, env = get_running_env(**args)
    system = env.layers[SYSTEM]
    with system as android, silence(system):
        with pytest.raises(AndroidSetupError):
            android(metadata)


@mock.patch("mozperftest.system.android.ADBLoggedDevice")
def test_android_perf_tuning_rooted(device):
    # Check to make sure that performance tuning runs
    # on rooted devices correctly
    device._have_su = True
    device._have_android_su = True
    device._have_root_shell = True
    device.is_rooted = True
    with mock.patch(
        "mozperftest.system.android_perf_tuner.PerformanceTuner.set_kernel_performance_parameters"
    ) as mockfunc:
        tuner = PerformanceTuner(device)
        tuner.tune_performance()
        mockfunc.assert_called()


@mock.patch("mozperftest.system.android.ADBLoggedDevice")
def test_android_perf_tuning_nonrooted(device):
    # Check to make sure that performance tuning runs
    # on non-rooted devices correctly
    device._have_su = False
    device._have_android_su = False
    device._have_root_shell = False
    device.is_rooted = False
    with mock.patch(
        "mozperftest.system.android_perf_tuner.PerformanceTuner.set_kernel_performance_parameters"
    ) as mockfunc:
        tuner = PerformanceTuner(device)
        tuner.tune_performance()
        mockfunc.assert_not_called()


class Device:
    def __init__(self, name, rooted=True):
        self.device_name = name
        self.is_rooted = rooted
        self.call_counts = 0

    @property
    def _logger(self):
        return self

    def noop(self, *args, **kw):
        pass

    debug = error = info = clear_logcat = noop

    def shell_bool(self, *args, **kw):
        self.call_counts += 1
        return True

    def shell_output(self, *args, **kw):
        self.call_counts += 1
        return self.device_name


def test_android_perf_tuning_all_calls():
    # Check without mocking PerformanceTuner functions
    for name in ["?"]:
        device = Device(name)
        tuner = PerformanceTuner(device)
        tuner.tune_performance()
        assert device.call_counts > 1


@mock.patch("mozperftest.system.VersionProducer", new=mock.MagicMock())
@mock.patch("mozperftest.system.android_perf_tuner.PerformanceTuner")
@mock.patch("mozperftest.system.android.ADBLoggedDevice")
def test_android_with_perftuning(device, tuner):
    args = {
        "app": "fenix",
        "flavor": "mobile-browser",
        "android-install-apk": ["this.apk"],
        "android": True,
        "android-timeout": 30,
        "android-capture-adb": "stdout",
        "android-app-name": "org.mozilla.fenix",
        "android-perf-tuning": True,
    }
    tuner.return_value = tuner

    mach_cmd, metadata, env = get_running_env(**args)
    system = env.layers[SYSTEM]
    with system as android, silence(system):
        android(metadata)

    # Make sure the tuner was actually called
    tuner.tune_performance.assert_called()


def test_android_failure():
    # no patching so it'll try for real and fail
    args = {
        "app": "fenix",
        "flavor": "mobile-browser",
        "android-install-apk": ["this"],
        "android": True,
        "android-timeout": 120,
        "android-app-name": "org.mozilla.fenix",
        "android-capture-adb": "stdout",
    }

    mach_cmd, metadata, env = get_running_env(**args)
    system = env.layers[SYSTEM]
    with system as android, silence(system), pytest.raises(DeviceError):
        android(metadata)


@mock.patch("mozperftest.system.VersionProducer", new=mock.MagicMock())
@mock.patch(
    "mozperftest.system.android.AndroidDevice.custom_apk_exists", new=lambda x: False
)
@mock.patch("mozperftest.utils.requests.get", new=requests_content())
@mock.patch("mozperftest.system.android.ADBLoggedDevice")
def test_android_apk_alias(device):
    args = {
        "app": "fenix",
        "flavor": "mobile-browser",
        "android-install-apk": ["fenix_nightly_armeabi_v7a"],
        "android": True,
        "android-app-name": "org.mozilla.fenix",
        "android-capture-adb": "stdout",
    }

    mach_cmd, metadata, env = get_running_env(**args)
    system = env.layers[SYSTEM]
    with system as android, silence(system):
        android(metadata)

    assert device.mock_calls[1][1][0] == "org.mozilla.fenix"
    assert device.mock_calls[2][1][0].endswith("target.apk")


@mock.patch("mozperftest.system.VersionProducer", new=mock.MagicMock())
@mock.patch("mozperftest.utils.requests.get", new=requests_content())
@mock.patch("mozperftest.system.android.ADBLoggedDevice")
def test_android_timeout(device):
    args = {
        "app": "fenix",
        "flavor": "mobile-browser",
        "android-install-apk": ["gve_nightly_api16"],
        "android": True,
        "android-timeout": 60,
        "android-app-name": "org.mozilla.geckoview_example",
        "android-capture-adb": "stdout",
    }

    mach_cmd, metadata, env = get_running_env(**args)
    system = env.layers[SYSTEM]
    with system as android, silence(system):
        android(metadata)
    options = device.mock_calls[0][-1]
    assert options["timeout"] == 60


@mock.patch("mozperftest.system.VersionProducer", new=mock.MagicMock())
@mock.patch("mozperftest.utils.requests.get", new=requests_content())
def test_android_log_adb():
    with temp_file() as log_adb:
        args = {
            "app": "fenix",
            "flavor": "mobile-browser",
            "android-install-apk": ["gve_nightly_api16"],
            "android": True,
            "android-timeout": 60,
            "android-app-name": "org.mozilla.geckoview_example",
            "android-capture-adb": log_adb,
        }

        mach_cmd, metadata, env = get_running_env(**args)
        system = env.layers[SYSTEM]
        with system as android, silence(system), pytest.raises(DeviceError):
            android(metadata)
        with open(log_adb) as f:
            assert "DEBUG ADBLoggedDevice" in f.read()


@mock.patch("mozperftest.system.VersionProducer", new=mock.MagicMock())
@mock.patch("mozperftest.utils.requests.get", new=requests_content())
@mock.patch("mozperftest.system.android.ADBLoggedDevice")
def test_android_log_cat(device):
    with temp_file() as log_cat:
        args = {
            "app": "fenix",
            "flavor": "mobile-browser",
            "android-install-apk": ["gve_nightly_api16"],
            "android": True,
            "android-timeout": 60,
            "android-app-name": "org.mozilla.geckoview_example",
            "android-capture-logcat": log_cat,
            "android-clear-logcat": True,
            "android-capture-adb": "stdout",
        }

        mach_cmd, metadata, env = get_running_env(**args)
        system = env.layers[SYSTEM]
        andro = get_android_device_layer(system.layers)

        with system as layer, silence(system):
            andro.device = device
            andro.device.get_logcat = mock.Mock(result_value=[])
            layer(metadata)

        andro.device.get_logcat.assert_called()
        andro.device.clear_logcat.assert_called()


@mock.patch("mozperftest.system.VersionProducer", new=mock.MagicMock())
@mock.patch("mozperftest.system.android.AndroidDevice.setup", new=mock.MagicMock)
@mock.patch("mozperftest.system.android.Path")
@mock.patch("mozperftest.system.android.ADBLoggedDevice", new=FakeDevice)
def test_android_custom_apk(mozperftest_android_path):
    args = {
        "app": "fenix",
        "flavor": "mobile-browser",
        "android": True,
    }

    with temp_file(name="user_upload.apk", content="") as sample_apk:
        sample_apk = pathlib.Path(sample_apk)
        mozperftest_android_path.return_value = sample_apk

        mach_cmd, metadata, env = get_running_env(**args)
        system = env.layers[SYSTEM]
        android = get_android_device_layer(system.layers)

        with system as _, silence(system):
            assert android._custom_apk_path is None
            assert android.custom_apk_exists()
            assert android.custom_apk_path == sample_apk

    mozperftest_android_path.assert_called_once()


@mock.patch("mozperftest.system.VersionProducer", new=mock.MagicMock())
@mock.patch("mozperftest.system.android.AndroidDevice.setup", new=mock.MagicMock)
@mock.patch("mozperftest.system.android.Path.exists")
@mock.patch("mozperftest.system.android.ADBLoggedDevice", new=FakeDevice)
def test_android_custom_apk_nonexistent(path_exists):
    args = {
        "app": "fenix",
        "flavor": "mobile-browser",
        "android": True,
    }

    path_exists.return_value = False

    mach_cmd, metadata, env = get_running_env(**args)
    system = env.layers[SYSTEM]
    android = get_android_device_layer(system.layers)

    with system as _, silence(system):
        assert android._custom_apk_path is None
        assert not android.custom_apk_exists()
        assert android.custom_apk_path is None

    path_exists.assert_called()


@mock.patch("mozperftest.system.VersionProducer", new=mock.MagicMock())
@mock.patch("mozperftest.system.android.Path")
@mock.patch("mozperftest.system.android.ADBLoggedDevice", new=FakeDevice)
def test_android_setup_custom_apk(mozperftest_android_path):
    args = {
        "app": "fenix",
        "flavor": "mobile-browser",
        "android": True,
    }

    with temp_file(name="user_upload.apk", content="") as sample_apk:
        sample_apk = pathlib.Path(sample_apk)
        mozperftest_android_path.return_value = sample_apk

        mach_cmd, metadata, env = get_running_env(**args)
        system = env.layers[SYSTEM]
        android = get_android_device_layer(system.layers)

        with system as _, silence(system):
            # The custom apk should be found immediately, and it
            # should replace any --android-install-apk settings
            assert android._custom_apk_path == sample_apk
            assert env.get_arg("android-install-apk") == [sample_apk]

    mozperftest_android_path.assert_called_once()


if __name__ == "__main__":
    mozunit.main()
