const { app, BrowserWindow, ipcMain } = require("electron");

// Relax Chromium restrictions for background audio playback
if (app) {
  app.commandLine.appendSwitch("autoplay-policy", "no-user-gesture-required");
  app.commandLine.appendSwitch("disable-renderer-backgrounding");
}

/**
 * Manages chunked byte/time ranges for track loading and caching.
 */
class CacheRange {
  constructor(end = -1) {
    this.ranges = [];
    this.end = end;
    this.cacheSize = 0;
  }

  /**
   * Sets the total expected size (bytes, samples, or total duration in seconds).
   * @param {number} size
   */
  setCacheSize(size) {
    this.cacheSize = size;
  }

  /**
   * Adds a new buffered range and automatically merges overlapping/touching intervals.
   * @param {number} start
   * @param {number} end
   */
  addRange(start, end) {
    if (start < end) {
      this.ranges.push({ start, end });
      this._normalizeRanges();
    }
  }

  /**
   * Checks if a given point (time, sample, or byte offset) is within the cached ranges.
   * @param {number} value
   * @returns {boolean}
   */
  contains(value) {
    return this.ranges.some(
      (range) => value >= range.start && value <= range.end,
    );
  }

  /**
   * Returns true if the cache covers from 0 up to the total cacheSize.
   * @returns {boolean}
   */
  isComplete() {
    if (this.end == -1) {
      return false;
    }
    if (this.ranges.length === 1) {
      return this.ranges[0].start === 0 && this.ranges[0].end >= this.end;
    }
    return false;
  }

  /**
   * Returns a deep copy of the current active ranges.
   * @returns {Array<{start: number, end: number}>}
   */
  getRangeCopy() {
    return this.ranges.map((range) => ({ ...range }));
  }

  /**
   * Internal helper to sort and merge overlapping or adjacent intervals.
   */
  _normalizeRanges() {
    if (this.ranges.length <= 1) return;

    // Sort ranges by start position
    this.ranges.sort((a, b) => a.start - b.start);

    const merged = [this.ranges[0]];

    for (let i = 1; i < this.ranges.length; i++) {
      const current = this.ranges[i];
      const lastMerged = merged[merged.length - 1];

      // If current range overlaps or touches the last merged range, merge them
      if (current.start <= lastMerged.end) {
        lastMerged.end = Math.max(lastMerged.end, current.end);
      } else {
        merged.push(current);
      }
    }

    this.ranges = merged;
  }
}

class AudioManager {
  constructor() {
    console.log(
      "[AudioManager] Initializing In-Memory Blob Audio Backend (Zero Temp Files)",
    );
    this._status = "Stopped";
    this._volume = 100;
    this._offset = 0;
    this._lookAhead = 5;
    this._currentTrack = null;
    this._nextTrack = null;
    this._currentTimeCacheRange = null;
    this._currentDataCacheRange = null;
    this._nextTimeCacheRange = null;
    this._nextDataCacheRange = null;
    this._deviceList = [{ name: "Default System Audio Device", type: "Audio" }];
    this._currentDevice = this._deviceList[0];
    this._deviceListener = null;

    this._audioWindow = null;
    this._isWindowReady = false;
    this._commandQueue = [];

    this._initAudioWindow();
  }

  _initAudioWindow() {
    const createWindow = () => {
      if (this._audioWindow && !this._audioWindow.isDestroyed()) return;

      this._audioWindow = new BrowserWindow({
        show: false,
        webPreferences: {
          nodeIntegration: true,
          contextIsolation: false,
        },
      });

      // HTML5 Audio element backed by in-memory Blob URLs (Full duration support)
      const playerHtml = `
                <html>
                <body>
                    <audio id="audioPlayer"></audio>
                    <script>
                        const audio = document.getElementById('audioPlayer');
                        const ipcRenderer = require('electron').ipcRenderer;
                        let currentBlobUrl = null;

                        ipcRenderer.on('audio-command', (event, { command, value, format }) => {
                            if (command === 'load') {
                                if (currentBlobUrl) {
                                    URL.revokeObjectURL(currentBlobUrl);
                                    currentBlobUrl = null;
                                }
                                // Create an in-memory Blob directly from the transferred ArrayBuffer
                                const mimeType = format === 'mp3' ? 'audio/mp3' : 'audio/flac';
                                const blob = new Blob([value], { type: mimeType });
                                currentBlobUrl = URL.createObjectURL(blob);
                                audio.src = currentBlobUrl;
                                audio.load();
                            } else if (command === 'play') {
                                audio.play().catch(err => ipcRenderer.send('audio-error', err.message));
                            } else if (command === 'pause') {
                                audio.pause();
                            } else if (command === 'volume') {
                                audio.volume = value / 100;
                            } else if (command === 'seek') {
                                audio.currentTime = value;
                            }
                        });

                        audio.addEventListener('timeupdate', () => {
                            ipcRenderer.send('audio-timeupdate', audio.currentTime);
                        });

                        audio.addEventListener('ended', () => {
                            ipcRenderer.send('audio-ended');
                        });

                        audio.addEventListener('error', (e) => {
                            ipcRenderer.send('audio-error', audio.error ? audio.error.message : 'Unknown audio error');
                        });
                    </script>
                </body>
                </html>
            `;

      this._audioWindow.loadURL(
        `data:text/html;charset=utf-8,${encodeURIComponent(playerHtml)}`,
      );
      this._isWindowReady = true;

      while (this._commandQueue.length > 0) {
        const { command, value, format } = this._commandQueue.shift();
        this._sendToAudioWindow(command, value, format);
      }
    };

    if (app && app.isReady()) {
      createWindow();
    } else if (app) {
      app.whenReady().then(createWindow);
    } else {
      setTimeout(createWindow, 1000);
    }

    ipcMain.on("audio-timeupdate", (event, currentTime) => {
      if (this._audioWindow && event.sender === this._audioWindow.webContents) {
        this._offset = currentTime;
      }
    });

    ipcMain.on("audio-ended", (event) => {
      if (this._audioWindow && event.sender === this._audioWindow.webContents) {
        this._status = "Stopped";
      }
    });

    ipcMain.on("audio-error", (event, errMsg) => {
      console.error("[AudioManager Audio Engine Error]:", errMsg);
    });
  }

  _sendToAudioWindow(command, value, format) {
    if (
      this._isWindowReady &&
      this._audioWindow &&
      !this._audioWindow.isDestroyed()
    ) {
      this._audioWindow.webContents.send("audio-command", {
        command,
        value,
        format,
      });
    } else {
      this._commandQueue.push({ command, value, format });
    }
  }

  /**
   * Opens a resource or file.
   * @param {boolean} isNextTrack - Is next track flag
   * @param {string} filePath - Audio file
   * @param {string} format - Format string ('flac', 'mp3')
   * @returns {*} Result of the open operation
   */
  open(isNextTrack, filePath, format) {
    // TODO
  }

  /**
   * Opens an audio stream with the specified parameters.
   * @param {boolean} isNextTrack - Is next track flag
   * @param {Buffer} sampleData - Sample data buffer
   * @param {string} format - Format string ('flac', 'mp3')
   * @param {number} sampleCount - Sample count / total samples
   * @returns {boolean} True if the stream opened successfully
   */
  openStream(isNextTrack, sampleData, format, sampleCount) {
    const sampleRate = format === "flac" ? 48000 : 44100;
    const duration = (sampleCount / sampleRate) * 1000;

    const trackInfo = {
      format: "FFmpeg",
      channel: 2,
      sampleRate: sampleRate,
      bitsPerSample: 24,
      lengthInSamples: sampleCount,
      duration: duration,
      buffer: sampleData,
      formatType: format,
    };

    const arrayBuffer = sampleData.buffer.slice(
      sampleData.byteOffset,
      sampleData.byteOffset + sampleData.byteLength,
    );

    if (isNextTrack) {
      this._nextTrack = trackInfo;
    } else {
      this._currentTrack = trackInfo;
      this._sendToAudioWindow("load", arrayBuffer, format);
    }

    return true;
  }

  /**
   * Closes a handle or stream.
   * @param {boolean} shouldFree - Whether to free the resources or not (true on termination)
   */
  close(shouldFree) {
    if (shouldFree) {
      this._currentTrack = null;
      this._nextTrack = null;
    }
  }

  /**
   * Starts playback.
   * @returns {string} Result of the play operation ('')
   */
  play() {
    this._status = "Playing";
    this._sendToAudioWindow("play");
    return "";
  }

  /**
   * Pauses playback.
   */
  pause() {
    this._status = "Paused";
    this._sendToAudioWindow("pause");
  }

  /**
   * Gets the current playback status.
   * @returns {string} Status ('Playing', 'Paused', 'Stopped')
   */
  status() {
    return this._status;
  }

  /**
   * Sets or gets the volume level.
   * @param {number} [volumeLevel] - Volume level integer (0-100)
   * @returns {undefined}
   */
  volume(volumeLevel) {
    if (volumeLevel !== undefined) {
      this._volume = volumeLevel;
      this._sendToAudioWindow("volume", volumeLevel);
    }
    return this._volume;
  }

  /**
   * Gets or sets the playback offset/position in seconds.
   * @param {number|undefined} [offsetSeconds] - Offset value in seconds to seek to (or undefined to query current position)
   * @returns {number} Current playback offset/position in seconds (float)
   */
  offset(offsetSeconds) {
    if (offsetSeconds !== undefined) {
      this._offset = offsetSeconds;
      this._sendToAudioWindow("seek", offsetSeconds);
    }
    return this._offset;
  }

  /**
   * Retrieves a list of available audio devices.
   * @returns {Array<{name: string, type: string}>} List of available devices
   * @example [{ name: 'Speakers (Realtek(R) Audio)', type: 'Windows Audio' }, { name: 'Primary Sound Driver', type: 'DirectSound' }]
   */
  getAvailableDeviceList() {
    return this._deviceList;
  }

  /**
   * Selects and uses a specific audio device.
   * @param {string} name - Device identifier or configuration
   * @param {string} type - Additional device parameter
   */
  useDevice(deviceId, deviceParam) {
    this._currentDevice = deviceId;
    return true;
  }

  /**
   * Retrieves information about the currently active device.
   * @returns {{name: string, type: string}} Device information object
   */
  deviceInformation() {
    return this._currentDevice;
  }

  /**
   * Retrieves information about a specific track.
   * @param {boolean} isNextTrack - Track identifier or index flag
   * @returns {{format: string, channel: number, sampleRate: number, bitsPerSample: number, lengthInSamples: number, duration: number}} Track information object
   */
  trackInformation(isNextTrack) {
    const track = isNextTrack ? this._nextTrack : this._currentTrack;
    return (
      track || {
        format: "FFmpeg",
        channel: 2,
        sampleRate: 48000,
        bitsPerSample: 24,
        lengthInSamples: 10890000,
        duration: 226875,
      }
    );
  }

  /**
   * Checks whether the current audio stream has finished.
   * @returns {boolean} True if the stream finished, false otherwise
   */
  hasStreamFinished() {
    return this._status === "Stopped";
  }

  /**
   * Releases allocated native resources.
   */
  releaseResources() {
    this.close(true);
    if (this._audioWindow && !this._audioWindow.isDestroyed()) {
      this._audioWindow.destroy();
    }
    this._status = "Stopped";
  }

  /**
   * Sets or gets the look-ahead time in seconds.
   * @param {number} [seconds] - Look-ahead time in seconds (int)
   */
  lookAheadInSeconds(seconds) {
    if (seconds !== undefined) {
      this._lookAhead = seconds;
    }
  }

  /**
   * Sets a callback listener for device change events.
   * @param {Function} callback - Callback function expecting: ({ deviceList, currentDevice })
   */
  setDeviceChangedListener(callback) {
    this._deviceListener = callback;
  }

  /**
   * Clears the queued next track.
   */
  clearNextTrack() {
    this._nextTrack = null;
    this._nextTrackTimeRange = null;
    this._nextTrackDataRange = null;
  }

  /**
   * Switches playback to the next track.
   */
  switchToNext() {
    if (this._nextTrack) {
      this._currentTrack = this._nextTrack;
      this._nextTrack = null;
      this._currentTrackTimeRange = this._nextTrackTimeRange;
      this._currentTrackDataRange = this._nextTrackDataRange;
      this._nextTrackTimeRange = null;
      this._nextTrackDataRange = null;
      this._offset = 0;
      const arrayBuffer = this._currentTrack.buffer.buffer.slice(
        this._currentTrack.buffer.byteOffset,
        this._currentTrack.buffer.byteOffset +
          this._currentTrack.buffer.byteLength,
      );
      this._sendToAudioWindow(
        "load",
        arrayBuffer,
        this._currentTrack.formatType,
      );
    }
  }

  /**
   * Initializes the cache range for streaming/playback.
   * @param {boolean} isNextTrack - Next track flag
   * @param {CacheRange} trackTimeRange - Configuration object A
   * @param {CacheRange} trackDataRange - Configuration object B
   */
  initCacheRange(isNextTrack, trackTimeRange, trackDataRange) {
    const trackInfo = this.trackInformation(isNextTrack);
    trackTimeRange.end = trackInfo.duration;
    //trackDataRange.setCacheSize(trackInfo.lengthInSamples);

    if (isNextTrack) {
      this._nextTrackTimeRange = trackTimeRange;
      this._nextTrackDataRange = trackDataRange;
    } else {
      this._currentTrackTimeRange = trackTimeRange;
      this._currentTrackDataRange = trackDataRange;
    }
  }

  /**
   * Creates new cache ranges.
   * @returns {CacheRange} New cache ranges object/structure
   */
  newCacheRanges() {
    return new CacheRange();
  }
}

module.exports = () => new AudioManager();
