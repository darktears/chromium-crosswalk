// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/metrics/ui/screen_info_metrics_provider.h"

#include "build/build_config.h"
#include "components/metrics/proto/system_profile.pb.h"
#include "ui/gfx/screen.h"

namespace metrics {

#if defined(OS_WIN)

#include <windows.h>

namespace {

struct ScreenDPIInformation {
  double max_dpi_x;
  double max_dpi_y;
};

// Called once for each connected monitor.
BOOL CALLBACK GetMonitorDPICallback(HMONITOR, HDC hdc, LPRECT, LPARAM dwData) {
  const double kMillimetersPerInch = 25.4;
  ScreenDPIInformation* screen_info =
      reinterpret_cast<ScreenDPIInformation*>(dwData);
  // Size of screen, in mm.
  DWORD size_x = GetDeviceCaps(hdc, HORZSIZE);
  DWORD size_y = GetDeviceCaps(hdc, VERTSIZE);
  double dpi_x = (size_x > 0) ?
      GetDeviceCaps(hdc, HORZRES) / (size_x / kMillimetersPerInch) : 0;
  double dpi_y = (size_y > 0) ?
      GetDeviceCaps(hdc, VERTRES) / (size_y / kMillimetersPerInch) : 0;
  screen_info->max_dpi_x = std::max(dpi_x, screen_info->max_dpi_x);
  screen_info->max_dpi_y = std::max(dpi_y, screen_info->max_dpi_y);
  return TRUE;
}

void WriteScreenDPIInformationProto(SystemProfileProto::Hardware* hardware) {
  HDC desktop_dc = GetDC(NULL);
  if (desktop_dc) {
    ScreenDPIInformation si = {0, 0};
    if (EnumDisplayMonitors(desktop_dc, NULL, GetMonitorDPICallback,
            reinterpret_cast<LPARAM>(&si))) {
      hardware->set_max_dpi_x(si.max_dpi_x);
      hardware->set_max_dpi_y(si.max_dpi_y);
    }
    ReleaseDC(GetDesktopWindow(), desktop_dc);
  }
}

}  // namespace

#endif  // defined(OS_WIN)

ScreenInfoMetricsProvider::ScreenInfoMetricsProvider() {
}

ScreenInfoMetricsProvider::~ScreenInfoMetricsProvider() {
}

void ScreenInfoMetricsProvider::ProvideSystemProfileMetrics(
    SystemProfileProto* system_profile_proto) {
  SystemProfileProto::Hardware* hardware =
      system_profile_proto->mutable_hardware();

  const gfx::Size display_size = GetScreenSize();
  hardware->set_primary_screen_width(display_size.width());
  hardware->set_primary_screen_height(display_size.height());
  hardware->set_primary_screen_scale_factor(GetScreenDeviceScaleFactor());
  hardware->set_screen_count(GetScreenCount());

#if defined(OS_WIN)
  WriteScreenDPIInformationProto(hardware);
#endif
}

gfx::Size ScreenInfoMetricsProvider::GetScreenSize() const {
  return gfx::Screen::GetNativeScreen()->GetPrimaryDisplay().GetSizeInPixel();
}

float ScreenInfoMetricsProvider::GetScreenDeviceScaleFactor() const {
  return gfx::Screen::GetNativeScreen()->
      GetPrimaryDisplay().device_scale_factor();
}

int ScreenInfoMetricsProvider::GetScreenCount() const {
  // TODO(scottmg): NativeScreen maybe wrong. http://crbug.com/133312
  return gfx::Screen::GetNativeScreen()->GetNumDisplays();
}

}  // namespace metrics
