/*
 *  Copyright (c) 2015 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

package org.webrtc.voiceengine;

import android.util.Log;
import org.webrtc.ThreadUtils;

import android.content.Context;
import android.media.AudioFormat;
import android.media.AudioRecord;
import android.media.MediaRecorder.AudioSource;
import android.os.Process;

import java.lang.System;
import java.nio.ByteBuffer;
import java.util.concurrent.TimeUnit;

import org.mozilla.gecko.annotation.WebRTCJNITarget;

@WebRTCJNITarget
public class WebRtcAudioRecord {
  private static final boolean DEBUG = false;

  private static final String TAG = "WebRtcAudioRecord";

  // Default audio data format is PCM 16 bit per sample.
  // Guaranteed to be supported by all devices.
  private static final int BITS_PER_SAMPLE = 16;

  // Requested size of each recorded buffer provided to the client.
  private static final int CALLBACK_BUFFER_SIZE_MS = 10;

  // Average number of callbacks per second.
  private static final int BUFFERS_PER_SECOND = 1000 / CALLBACK_BUFFER_SIZE_MS;

  // We ask for a native buffer size of BUFFER_SIZE_FACTOR * (minimum required
  // buffer size). The extra space is allocated to guard against glitches under
  // high load.
  private static final int BUFFER_SIZE_FACTOR = 2;

  // The AudioRecordJavaThread is allowed to wait for successful call to join()
  // but the wait times out afther this amount of time.
  private static final long AUDIO_RECORD_THREAD_JOIN_TIMEOUT_MS = 2000;

  private final long nativeAudioRecord;
  private final Context context;

  private WebRtcAudioEffects effects = null;

  private ByteBuffer byteBuffer;

  private AudioRecord audioRecord = null;
  private AudioRecordThread audioThread = null;

  private static volatile boolean microphoneMute = false;
  private byte[] emptyBytes;

  // Audio recording error handler functions.
  public static interface WebRtcAudioRecordErrorCallback {
    void onWebRtcAudioRecordInitError(String errorMessage);
    void onWebRtcAudioRecordStartError(String errorMessage);
    void onWebRtcAudioRecordError(String errorMessage);
  }

  private static WebRtcAudioRecordErrorCallback errorCallback = null;

  public static void setErrorCallback(WebRtcAudioRecordErrorCallback errorCallback) {
    Log.d(TAG, "Set error callback");
    WebRtcAudioRecord.errorCallback = errorCallback;
  }

  /**
   * Audio thread which keeps calling ByteBuffer.read() waiting for audio
   * to be recorded. Feeds recorded data to the native counterpart as a
   * periodic sequence of callbacks using DataIsRecorded().
   * This thread uses a Process.THREAD_PRIORITY_URGENT_AUDIO priority.
   */
  private class AudioRecordThread extends Thread {
    private volatile boolean keepAlive = true;

    public AudioRecordThread(String name) {
      super(name);
    }

    @Override
    public void run() {
      Process.setThreadPriority(Process.THREAD_PRIORITY_URGENT_AUDIO);
      Log.d(TAG, "AudioRecordThread" + WebRtcAudioUtils.getThreadInfo());
      assertTrue(audioRecord.getRecordingState() == AudioRecord.RECORDSTATE_RECORDING);

      long lastTime = System.nanoTime();
      while (keepAlive) {
        int bytesRead = audioRecord.read(byteBuffer, byteBuffer.capacity());
        if (bytesRead == byteBuffer.capacity()) {
          if (microphoneMute) {
            byteBuffer.clear();
            byteBuffer.put(emptyBytes);
          }
          nativeDataIsRecorded(bytesRead, nativeAudioRecord);
        } else {
          String errorMessage = "AudioRecord.read failed: " + bytesRead;
          Log.e(TAG, errorMessage);
          if (bytesRead == AudioRecord.ERROR_INVALID_OPERATION) {
            keepAlive = false;
            reportWebRtcAudioRecordError(errorMessage);
          }
        }
        if (DEBUG) {
          long nowTime = System.nanoTime();
          long durationInMs = TimeUnit.NANOSECONDS.toMillis((nowTime - lastTime));
          lastTime = nowTime;
          Log.d(TAG, "bytesRead[" + durationInMs + "] " + bytesRead);
        }
      }

      try {
        if (audioRecord != null) {
          audioRecord.stop();
        }
      } catch (IllegalStateException e) {
        Log.e(TAG, "AudioRecord.stop failed: " + e.getMessage());
      }
    }

    // Stops the inner thread loop and also calls AudioRecord.stop().
    // Does not block the calling thread.
    public void stopThread() {
      Log.d(TAG, "stopThread");
      keepAlive = false;
    }
  }

  WebRtcAudioRecord(Context context, long nativeAudioRecord) {
    Log.d(TAG, "ctor" + WebRtcAudioUtils.getThreadInfo());
    this.context = context;
    this.nativeAudioRecord = nativeAudioRecord;
    if (DEBUG) {
      WebRtcAudioUtils.logDeviceInfo(TAG);
    }
    effects = WebRtcAudioEffects.create();
  }

  private boolean enableBuiltInAEC(boolean enable) {
    Log.d(TAG, "enableBuiltInAEC(" + enable + ')');
    if (effects == null) {
      Log.e(TAG, "Built-in AEC is not supported on this platform");
      return false;
    }
    return effects.setAEC(enable);
  }

  private boolean enableBuiltInNS(boolean enable) {
    Log.d(TAG, "enableBuiltInNS(" + enable + ')');
    if (effects == null) {
      Log.e(TAG, "Built-in NS is not supported on this platform");
      return false;
    }
    return effects.setNS(enable);
  }

  private int initRecording(int sampleRate, int channels) {
    Log.d(TAG, "initRecording(sampleRate=" + sampleRate + ", channels=" + channels + ")");
    if (!WebRtcAudioUtils.hasPermission(context, android.Manifest.permission.RECORD_AUDIO)) {
      reportWebRtcAudioRecordInitError("RECORD_AUDIO permission is missing");
      return -1;
    }
    if (audioRecord != null) {
      reportWebRtcAudioRecordInitError("InitRecording called twice without StopRecording.");
      return -1;
    }
    final int bytesPerFrame = channels * (BITS_PER_SAMPLE / 8);
    final int framesPerBuffer = sampleRate / BUFFERS_PER_SECOND;
    byteBuffer = ByteBuffer.allocateDirect(bytesPerFrame * framesPerBuffer);
    Log.d(TAG, "byteBuffer.capacity: " + byteBuffer.capacity());
    emptyBytes = new byte[byteBuffer.capacity()];
    // Rather than passing the ByteBuffer with every callback (requiring
    // the potentially expensive GetDirectBufferAddress) we simply have the
    // the native class cache the address to the memory once.
    nativeCacheDirectBufferAddress(byteBuffer, nativeAudioRecord);

    // Get the minimum buffer size required for the successful creation of
    // an AudioRecord object, in byte units.
    // Note that this size doesn't guarantee a smooth recording under load.
    final int channelConfig = channelCountToConfiguration(channels);
    int minBufferSize =
        AudioRecord.getMinBufferSize(sampleRate, channelConfig, AudioFormat.ENCODING_PCM_16BIT);
    if (minBufferSize == AudioRecord.ERROR || minBufferSize == AudioRecord.ERROR_BAD_VALUE) {
      reportWebRtcAudioRecordInitError("AudioRecord.getMinBufferSize failed: " + minBufferSize);
      return -1;
    }
    Log.d(TAG, "AudioRecord.getMinBufferSize: " + minBufferSize);

    // Use a larger buffer size than the minimum required when creating the
    // AudioRecord instance to ensure smooth recording under load. It has been
    // verified that it does not increase the actual recording latency.
    int bufferSizeInBytes = Math.max(BUFFER_SIZE_FACTOR * minBufferSize, byteBuffer.capacity());
    Log.d(TAG, "bufferSizeInBytes: " + bufferSizeInBytes);

    int audioSource = AudioSource.VOICE_COMMUNICATION;
    if (android.os.Build.VERSION.SDK_INT < 11) { // XXX REMOVE BEFORE CHECKIN? Do we still need to support this API?
        audioSource = AudioSource.DEFAULT;
    }

    try {
      audioRecord = new AudioRecord(audioSource, sampleRate, channelConfig,
          AudioFormat.ENCODING_PCM_16BIT, bufferSizeInBytes);
    } catch (IllegalArgumentException e) {
      reportWebRtcAudioRecordInitError("AudioRecord ctor error: " + e.getMessage());
      releaseAudioResources();
      return -1;
    }
    if (audioRecord == null || audioRecord.getState() != AudioRecord.STATE_INITIALIZED) {
      reportWebRtcAudioRecordInitError("Failed to create a new AudioRecord instance");
      releaseAudioResources();
      return -1;
    }
    if (effects != null) {
      effects.enable(audioRecord.getAudioSessionId());
    }
    logMainParameters();
    logMainParametersExtended();
    return framesPerBuffer;
  }

  private boolean startRecording() {
    Log.d(TAG, "startRecording");
    assertTrue(audioRecord != null);
    assertTrue(audioThread == null);
    try {
      audioRecord.startRecording();
    } catch (IllegalStateException e) {
      reportWebRtcAudioRecordStartError("AudioRecord.startRecording failed: " + e.getMessage());
      return false;
    }
    if (audioRecord.getRecordingState() != AudioRecord.RECORDSTATE_RECORDING) {
      reportWebRtcAudioRecordStartError("AudioRecord.startRecording failed - incorrect state :"
          + audioRecord.getRecordingState());
      return false;
    }
    audioThread = new AudioRecordThread("AudioRecordJavaThread");
    audioThread.start();
    return true;
  }

  private boolean stopRecording() {
    Log.d(TAG, "stopRecording");
    assertTrue(audioThread != null);
    audioThread.stopThread();
    if (!ThreadUtils.joinUninterruptibly(audioThread, AUDIO_RECORD_THREAD_JOIN_TIMEOUT_MS)) {
      Log.e(TAG, "Join of AudioRecordJavaThread timed out");
    }
    audioThread = null;
    if (effects != null) {
      effects.release();
    }
    releaseAudioResources();
    return true;
  }

  private void logMainParameters() {
    Log.d(TAG, "AudioRecord: "
            + "session ID: " + audioRecord.getAudioSessionId() + ", "
            + "channels: " + audioRecord.getChannelCount() + ", "
            + "sample rate: " + audioRecord.getSampleRate());
  }

  private void logMainParametersExtended() {
    if (WebRtcAudioUtils.runningOnMarshmallowOrHigher()) {
      Log.d(TAG, "AudioRecord: "
              // The frame count of the native AudioRecord buffer.
              + "buffer size in frames: " + audioRecord.getBufferSizeInFrames());
    }
  }

  // Helper method which throws an exception  when an assertion has failed.
  private static void assertTrue(boolean condition) {
    if (!condition) {
      throw new AssertionError("Expected condition to be true");
    }
  }

  private int channelCountToConfiguration(int channels) {
    return (channels == 1 ? AudioFormat.CHANNEL_IN_MONO : AudioFormat.CHANNEL_IN_STEREO);
  }

  private native void nativeCacheDirectBufferAddress(ByteBuffer byteBuffer, long nativeAudioRecord);

  private native void nativeDataIsRecorded(int bytes, long nativeAudioRecord);

  // Sets all recorded samples to zero if |mute| is true, i.e., ensures that
  // the microphone is muted.
  public static void setMicrophoneMute(boolean mute) {
    Log.w(TAG, "setMicrophoneMute(" + mute + ")");
    microphoneMute = mute;
  }

  // Releases the native AudioRecord resources.
  private void releaseAudioResources() {
    if (audioRecord != null) {
      audioRecord.release();
      audioRecord = null;
    }
  }

  private void reportWebRtcAudioRecordInitError(String errorMessage) {
    Log.e(TAG, "Init recording error: " + errorMessage);
    if (errorCallback != null) {
      errorCallback.onWebRtcAudioRecordInitError(errorMessage);
    }
  }

  private void reportWebRtcAudioRecordStartError(String errorMessage) {
    Log.e(TAG, "Start recording error: " + errorMessage);
    if (errorCallback != null) {
      errorCallback.onWebRtcAudioRecordStartError(errorMessage);
    }
  }

  private void reportWebRtcAudioRecordError(String errorMessage) {
    Log.e(TAG, "Run-time recording error: " + errorMessage);
    if (errorCallback != null) {
      errorCallback.onWebRtcAudioRecordError(errorMessage);
    }
  }
}
