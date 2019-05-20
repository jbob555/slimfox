/*
 *  Copyright (c) 2014 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#ifndef WEBRTC_MODULES_DESKTOP_CAPTURE_CROPPING_WINDOW_CAPTURER_H_
#define WEBRTC_MODULES_DESKTOP_CAPTURE_CROPPING_WINDOW_CAPTURER_H_

#include <memory>

#include "webrtc/modules/desktop_capture/desktop_capturer.h"
#include "webrtc/modules/desktop_capture/desktop_capture_options.h"

namespace webrtc {

// WindowCapturer implementation that uses a screen capturer to capture the
// whole screen and crops the video frame to the window area when the captured
// window is on top.
class CroppingWindowCapturer : public DesktopCapturer,
                               public DesktopCapturer::Callback {
 public:
  static std::unique_ptr<DesktopCapturer> CreateCapturer(
      const DesktopCaptureOptions& options);

  ~CroppingWindowCapturer() override;

  // DesktopCapturer implementation.
  void Start(DesktopCapturer::Callback* callback) override;
  void Stop() override;
  void SetSharedMemoryFactory(
      std::unique_ptr<SharedMemoryFactory> shared_memory_factory) override;
  void CaptureFrame() override;
  void SetExcludedWindow(WindowId window) override;
  bool GetSourceList(SourceList* sources) override;
  bool SelectSource(SourceId id) override;
  bool FocusOnSelectedSource() override;

  // DesktopCapturer::Callback implementation, passed to |screen_capturer_| to
  // intercept the capture result.
  void OnCaptureResult(DesktopCapturer::Result result,
                       std::unique_ptr<DesktopFrame> frame) override;

 protected:
  explicit CroppingWindowCapturer(const DesktopCaptureOptions& options);

  // The platform implementation should override these methods.

  // Returns true if it is OK to capture the whole screen and crop to the
  // selected window, i.e. the selected window is opaque, rectangular, and not
  // occluded.
  virtual bool ShouldUseScreenCapturer() = 0;

  // Returns the window area relative to the top left of the virtual screen
  // within the bounds of the virtual screen.
  virtual DesktopRect GetWindowRectInVirtualScreen() = 0;

  WindowId selected_window() const { return selected_window_; }
  WindowId excluded_window() const { return excluded_window_; }

 private:
  DesktopCaptureOptions options_;
  DesktopCapturer::Callback* callback_;
  std::unique_ptr<DesktopCapturer> window_capturer_;
  std::unique_ptr<DesktopCapturer> screen_capturer_;
  SourceId selected_window_;
  WindowId excluded_window_;
};

}  // namespace webrtc

#endif  // WEBRTC_MODULES_DESKTOP_CAPTURE_CROPPING_WINDOW_CAPTURER_H_
