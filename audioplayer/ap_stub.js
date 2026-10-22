// A stub showing the signatures expected by Qobuz for the juce.node extension

/**
 * Manages chunked byte/time ranges for track loading and caching.
 */
class CacheRange {
  /**
   * Sets the total expected size (bytes, samples, or total duration in seconds).
   * @param {number} size
   */
  setCacheSize(size) {
  }

  /**
   * Adds a new buffered range and automatically merges overlapping/touching intervals.
   * @param {number} start
   * @param {number} end
   */
  addRange(start, end) {
  }

  /**
   * Checks if a given point (time, sample, or byte offset) is within the cached ranges.
   * @param {number} value
   * @returns {boolean}
   */
  contains(value) {
  }

  /**
   * Returns true if the cache covers from 0 up to the total cacheSize.
   * @returns {boolean}
   */
  isComplete() {
  }

  /**
   * Returns a deep copy of the current active ranges.
   * @returns {Array<{start: number, end: number}>}
   */
  getRangeCopy() {
  }
}

/**
 * @typedef {Object} Device
 * @property {string} name - The name of the device.
 * @property {string} type - The type/backend of the device.
 */

/**
 * @typedef {Object} TrackInformation
 * @property {string} format - The format of the track (flac, mp3)
 * @property {number} channel - The channel count (int)
 * @property {number} sampleRate - The sample rate (int)
 * @property {number} bitsPerSample - The BITS per sample (int)
 * @property {number} lengthInSamples - The length of the trakc in samples (unused)
 * @property {number} duration - The duration of the track in ms (not s)
*/

class AudioHelper {
    constructor() {
    }

    /**
     * Opens an audio file.
     * @param {boolean} isNextTrack - Is next track flag
     * @param {string} filePath - Audio file
     * @param {string} format - Format string ('flac', 'mp3')
     * @returns {boolean} Result of the open operation
     */
    open(isNextTrack, filePath, format) {
    }

    /**
     * Opens an audio stream .
     * @param {boolean} isNextTrack - Is next track flag
     * @param {Buffer} sampleData - Sample data buffer
     * @param {string} format - Format string ('flac', 'mp3')
     * @param {number} sampleCount - Sample count / total samples
     * @returns {boolean} True if the stream opened successfully
     */
    openStream(isNextTrack, sampleData, format, sampleCount) {
    }

    /**
     * Closes a handle or stream.
     * @param {boolean} shouldFree - Whether to free the resources or not (true on termination)
     */
    close(shouldFree) {
    }

    /**
     * Starts playback.
     * @returns {string} - an empty string ''
     */
    play() {
    }

    /**
     * Pauses playback.
     */
    pause() {
    }

    /**
     * Gets the current playback status.
     * @returns {string} Status ('Playing', 'Stopped', 'Blocked')
     */
    status() {
    }

    /**
     * Sets or gets the volume level.
     * @param {number} [volumeLevel] - Volume level integer (0-100)
     */
    volume(volumeLevel) {
    }

    /**
     * Gets or sets the playback offset/position in seconds.
     * @param {number|undefined} [offsetSeconds] - Offset value in seconds to seek to (or undefined to query current position)
     * @returns {number} Current playback offset/position in seconds (float)
     */
    offset(offsetSeconds) {
    }

    /**
     * Retrieves a list of available audio devices.
     * @returns {Array<Device>} List of available devices
     */
    getAvailableDeviceList() {
    }

    /**
     * Selects and uses a specific audio device, name and type of a {Device}
     * @param {string} name
     * @param {string} type
     */
    useDevice(name, type) {
    }

    /**
     * Retrieves information about the currently active device.
     * @returns {Device} Device information object
     */
    deviceInformation() {
    }

    /**
     * Retrieves information about a specific track.
     * @param {boolean} isNextTrack - Track identifier or index flag
     * @returns {TrackInformation} Track information object
     */
    trackInformation(isNextTrack) {
    }

    /**
     * Checks whether the current audio stream has finished.
     * @returns {boolean} True if the stream finished, false otherwise
     */
    hasStreamFinished() {
    }

    /**
     * Releases allocated native resources.
     */
    releaseResources() {
    }

    /**
     * Sets or gets the look-ahead time in seconds.
     * @param {number} [seconds] - Look-ahead time in seconds (int)
     */
    lookAheadInSeconds(seconds) {
    }

    /**
     * Sets a callback listener for device change events.
     * @param {Function} callback - Callback function expecting: ({ deviceList, currentDevice })
     */
    setDeviceChangedListener(callback) {
    }

    /**
     * Clears the queued next track.
     */
    clearNextTrack() {
    }

    /**
     * Switches playback to the next track.
     */
    switchToNext() {
    }

    /**
     * Initializes the cache range for streaming/playback.
     * @param {boolean} isNextTrack - Next track flag
     * @param {CacheRange} trackTimeRange - Configuration object A
     * @param {CacheRange} trackDataRange - Configuration object B
     */
    initCacheRange(isNextTrack, trackTimeRange, trackDataRange) {
    }


    /**
     * Creates new cache ranges.
     * @returns {CacheRange} New cache ranges object/structure
     */
    newCacheRanges() {
    }
}
