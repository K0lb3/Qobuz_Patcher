#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio.h"
#include <node_api.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>

// Dynamic Interval structure mirroring juce::SparseSet range management
typedef struct {
    double start;
    double end;
} CacheInterval;

typedef struct {
    CacheInterval* intervals;
    int count;
    int capacity;
    double cache_size;
    ma_bool32 is_complete;
} CacheRangeObj;

// Player instance structure wrapping miniaudio engine, decoder, and sound
typedef struct {
    ma_engine engine;
    ma_decoder decoder;
    ma_sound sound;
    ma_bool32 engine_initialized;
    ma_bool32 sound_initialized;
    ma_bool32 decoder_initialized;
    napi_ref stream_buffer_ref;

    // Next track fields
    ma_decoder next_decoder;
    ma_sound next_sound;
    ma_bool32 next_decoder_initialized;
    ma_bool32 next_sound_initialized;
    napi_ref next_stream_buffer_ref;
    char next_format_str[32];
    ma_uint64 next_length_in_samples;

    int status; // 0 = Stopped, 1 = Playing, 2 = Paused
    float volume; // 0 - 100
    double look_ahead;
    char device_name[256];
    char device_type[256];
    ma_device_id playbackDeviceID;
    ma_bool32 hasDeviceID;
    ma_uint32 sample_rate;
    ma_uint32 channels;
    ma_uint64 length_in_samples;
    char format_str[32];
    napi_ref device_changed_listener_ref;
    napi_env env;
} PlayerInstance;

// Forward declarations of CacheRange methods
static napi_value CacheRange_addRange(napi_env env, napi_callback_info info);
static napi_value CacheRange_contains(napi_env env, napi_callback_info info);
static napi_value CacheRange_getRangeCopy(napi_env env, napi_callback_info info);
static napi_value CacheRange_isComplete(napi_env env, napi_callback_info info);
static napi_value CacheRange_isEmpty(napi_env env, napi_callback_info info);
static napi_value CacheRange_setCacheSize(napi_env env, napi_callback_info info);

// Forward declarations of Juce inner object methods
static napi_value Juce_open(napi_env env, napi_callback_info info);
static napi_value Juce_openStream(napi_env env, napi_callback_info info);
static napi_value Juce_close(napi_env env, napi_callback_info info);
static napi_value Juce_play(napi_env env, napi_callback_info info);
static napi_value Juce_pause(napi_env env, napi_callback_info info);
static napi_value Juce_status(napi_env env, napi_callback_info info);
static napi_value Juce_volume(napi_env env, napi_callback_info info);
static napi_value Juce_offset(napi_env env, napi_callback_info info);
static napi_value Juce_getAvailableDeviceList(napi_env env, napi_callback_info info);
static napi_value Juce_useDevice(napi_env env, napi_callback_info info);
static napi_value Juce_deviceInformation(napi_env env, napi_callback_info info);
static napi_value Juce_trackInformation(napi_env env, napi_callback_info info);
static napi_value Juce_hasStreamFinished(napi_env env, napi_callback_info info);
static napi_value Juce_initCacheRange(napi_env env, napi_callback_info info);
static napi_value Juce_releaseResources(napi_env env, napi_callback_info info);
static napi_value Juce_lookAheadInSeconds(napi_env env, napi_callback_info info);
static napi_value Juce_setDeviceChangedListener(napi_env env, napi_callback_info info);
static napi_value Juce_clearNextTrack(napi_env env, napi_callback_info info);
static napi_value Juce_switchToNext(napi_env env, napi_callback_info info);
static napi_value Juce_newCacheRanges(napi_env env, napi_callback_info info);

// --- CacheRange Implementation (SparseSet Behavior) ---

static void CacheRange_finalize(napi_env env, void* finalize_data, void* finalize_hint) {
    CacheRangeObj* obj = (CacheRangeObj*)finalize_data;
    if (obj) {
        if (obj->intervals) {
            free(obj->intervals);
        }
        free(obj);
    }
}

static void CacheRange_addRangeInternal(CacheRangeObj* obj, double start, double end) {
    if (start >= end) return;

    // Dynamically expand capacity if needed
    if (obj->count >= obj->capacity) {
        int new_cap = obj->capacity == 0 ? 16 : obj->capacity * 2;
        CacheInterval* new_intervals = (CacheInterval*)realloc(obj->intervals, new_cap * sizeof(CacheInterval));
        if (!new_intervals) return;
        obj->intervals = new_intervals;
        obj->capacity = new_cap;
    }

    // Insert while maintaining sorted order and merging overlapping/adjacent ranges (SparseSet logic)
    int i = 0;
    while (i < obj->count && obj->intervals[i].end < start) {
        i++;
    }

    double merged_start = start;
    double merged_end = end;

    // Merge subsequent overlapping or touching intervals
    while (i < obj->count && obj->intervals[i].start <= merged_end) {
        if (obj->intervals[i].start < merged_start) {
            merged_start = obj->intervals[i].start;
        }
        if (obj->intervals[i].end > merged_end) {
            merged_end = obj->intervals[i].end;
        }
        // Shift remaining intervals left
        for (int j = i; j < obj->count - 1; j++) {
            obj->intervals[j] = obj->intervals[j + 1];
        }
        obj->count--;
    }

    // Insert the newly merged interval at position i
    for (int j = obj->count; j > i; j--) {
        obj->intervals[j] = obj->intervals[j - 1];
    }
    obj->intervals[i].start = merged_start;
    obj->intervals[i].end = merged_end;
    obj->count++;

    // Evaluate completeness against cache_size
    if (obj->cache_size > 0) {
        for (int k = 0; k < obj->count; k++) {
            if (obj->intervals[k].start <= 0.0 && obj->intervals[k].end >= obj->cache_size) {
                obj->is_complete = MA_TRUE;
                break;
            }
        }
    }
}

static napi_value CacheRange_addRange(napi_env env, napi_callback_info info) {
    size_t argc = 2;
    napi_value args[2];
    napi_value thiz;
    napi_get_cb_info(env, info, &argc, args, &thiz, NULL);

    CacheRangeObj* obj;
    napi_unwrap(env, thiz, (void**)&obj);

    double start = 0, end = 0;
    if (argc > 0 && args[0] != NULL) napi_get_value_double(env, args[0], &start);
    if (argc > 1 && args[1] != NULL) napi_get_value_double(env, args[1], &end);

    if (obj) {
        CacheRange_addRangeInternal(obj, start, end);
    }

    napi_value undefined;
    napi_get_undefined(env, &undefined);
    return undefined;
}

static napi_value CacheRange_contains(napi_env env, napi_callback_info info) {
    size_t argc = 1;
    napi_value args[1];
    napi_value thiz;
    napi_get_cb_info(env, info, &argc, args, &thiz, NULL);

    CacheRangeObj* obj;
    napi_unwrap(env, thiz, (void**)&obj);

    double start = 0;
    if (argc > 0 && args[0] != NULL) napi_get_value_double(env, args[0], &start);

    ma_bool32 found = MA_FALSE;
    if (obj) {
        for (int i = 0; i < obj->count; i++) {
            if (start >= obj->intervals[i].start && start <= obj->intervals[i].end) {
                found = MA_TRUE;
                break;
            }
        }
    }

    napi_value result;
    napi_get_boolean(env, found, &result);
    return result;
}

static napi_value CacheRange_getRangeCopy(napi_env env, napi_callback_info info) {
    napi_value thiz;
    napi_get_cb_info(env, info, NULL, NULL, &thiz, NULL);
    CacheRangeObj* obj;
    napi_unwrap(env, thiz, (void**)&obj);

    napi_value arr;
    napi_create_array(env, &arr);

    if (obj) {
        for (int i = 0; i < obj->count; i++) {
            napi_value item;
            napi_create_object(env, &item);

            napi_value vStart, vEnd;
            napi_create_double(env, obj->intervals[i].start, &vStart);
            napi_create_double(env, obj->intervals[i].end, &vEnd);

            napi_set_named_property(env, item, "start", vStart);
            napi_set_named_property(env, item, "end", vEnd);

            napi_set_element(env, arr, i, item);
        }
    }

    return arr;
}

static napi_value CacheRange_isComplete(napi_env env, napi_callback_info info) {
    napi_value thiz;
    napi_get_cb_info(env, info, NULL, NULL, &thiz, NULL);
    CacheRangeObj* obj;
    napi_unwrap(env, thiz, (void**)&obj);

    ma_bool32 complete = obj ? obj->is_complete : MA_FALSE;
    if (obj && obj->count > 0 && obj->cache_size > 0) {
        for (int i = 0; i < obj->count; i++) {
            if (obj->intervals[i].start <= 0.0 && obj->intervals[i].end >= obj->cache_size) {
                complete = MA_TRUE;
                break;
            }
        }
    }

    napi_value result;
    napi_get_boolean(env, complete, &result);
    return result;
}

static napi_value CacheRange_isEmpty(napi_env env, napi_callback_info info) {
    napi_value thiz;
    napi_get_cb_info(env, info, NULL, NULL, &thiz, NULL);
    CacheRangeObj* obj;
    napi_unwrap(env, thiz, (void**)&obj);

    ma_bool32 empty = (obj == NULL || obj->count == 0) ? MA_TRUE : MA_FALSE;
    napi_value result;
    napi_get_boolean(env, empty, &result);
    return result;
}

static napi_value CacheRange_setCacheSize(napi_env env, napi_callback_info info) {
    size_t argc = 1;
    napi_value args[1];
    napi_value thiz;
    napi_get_cb_info(env, info, &argc, args, &thiz, NULL);

    CacheRangeObj* obj;
    napi_unwrap(env, thiz, (void**)&obj);

    double size = 0;
    if (argc > 0 && args[0] != NULL) napi_get_value_double(env, args[0], &size);
    if (obj) {
        obj->cache_size = size;
        if (obj->cache_size > 0 && obj->count > 0) {
            for (int i = 0; i < obj->count; i++) {
                if (obj->intervals[i].start <= 0.0 && obj->intervals[i].end >= obj->cache_size) {
                    obj->is_complete = MA_TRUE;
                    break;
                }
            }
        }
    }

    napi_value undefined;
    napi_get_undefined(env, &undefined);
    return undefined;
}

// --- PlayerInstance Finalizer ---

static void PlayerInstance_finalize(napi_env env, void* finalize_data, void* finalize_hint) {
    PlayerInstance* player = (PlayerInstance*)finalize_data;
    if (player) {
        if (player->sound_initialized) {
            ma_sound_stop(&player->sound);
            ma_sound_uninit(&player->sound);
        }
        if (player->decoder_initialized) {
            ma_decoder_uninit(&player->decoder);
        }
        if (player->stream_buffer_ref) {
            napi_delete_reference(env, player->stream_buffer_ref);
        }
        if (player->next_sound_initialized) {
            ma_sound_stop(&player->next_sound);
            ma_sound_uninit(&player->next_sound);
        }
        if (player->next_decoder_initialized) {
            ma_decoder_uninit(&player->next_decoder);
        }
        if (player->next_stream_buffer_ref) {
            napi_delete_reference(env, player->next_stream_buffer_ref);
        }
        if (player->engine_initialized) {
            ma_engine_uninit(&player->engine);
        }
        if (player->device_changed_listener_ref) {
            napi_delete_reference(env, player->device_changed_listener_ref);
        }
        free(player);
    }
}

// --- Juce Inner Methods ---

static napi_value Juce_open(napi_env env, napi_callback_info info) {
    size_t argc = 3;
    napi_value args[3];
    napi_value thiz;
    napi_get_cb_info(env, info, &argc, args, &thiz, NULL);

    PlayerInstance* player;
    napi_unwrap(env, thiz, (void**)&player);

    char filepath[512] = {0};
    size_t len = 0;
    if (argc > 1 && args[1] != NULL) {
        napi_valuetype vtype;
        napi_typeof(env, args[1], &vtype);
        if (vtype == napi_string) {
            napi_get_value_string_utf8(env, args[1], filepath, sizeof(filepath), &len);
        }
    }

    ma_bool32 is_next = MA_FALSE;
    if (argc > 2 && args[2] != NULL) {
        bool b = false;
        napi_get_value_bool(env, args[2], &b);
        is_next = b ? MA_TRUE : MA_FALSE;
    }

    ma_bool32 success = MA_FALSE;
    if (player) {
        if (is_next) {
            if (player->next_sound_initialized) {
                ma_sound_stop(&player->next_sound);
                ma_sound_uninit(&player->next_sound);
                player->next_sound_initialized = MA_FALSE;
            }
            if (player->next_decoder_initialized) {
                ma_decoder_uninit(&player->next_decoder);
                player->next_decoder_initialized = MA_FALSE;
            }
            if (player->next_stream_buffer_ref) {
                napi_delete_reference(env, player->next_stream_buffer_ref);
                player->next_stream_buffer_ref = NULL;
            }

            if (ma_decoder_init_file(filepath, NULL, &player->next_decoder) == MA_SUCCESS) {
                player->next_decoder_initialized = MA_TRUE;
                ma_decoder_get_length_in_pcm_frames(&player->next_decoder, &player->next_length_in_samples);
                strcpy(player->next_format_str, "FFmpeg");

                if (player->engine_initialized) {
                    if (ma_sound_init_from_data_source(&player->engine, &player->next_decoder, 0, NULL, &player->next_sound) == MA_SUCCESS) {
                        player->next_sound_initialized = MA_TRUE;
                    }
                }
                success = MA_TRUE;
            }
        } else {
            if (player->sound_initialized) {
                ma_sound_stop(&player->sound);
                ma_sound_uninit(&player->sound);
                player->sound_initialized = MA_FALSE;
            }
            if (player->decoder_initialized) {
                ma_decoder_uninit(&player->decoder);
                player->decoder_initialized = MA_FALSE;
            }
            if (player->stream_buffer_ref) {
                napi_delete_reference(env, player->stream_buffer_ref);
                player->stream_buffer_ref = NULL;
            }

            if (ma_decoder_init_file(filepath, NULL, &player->decoder) == MA_SUCCESS) {
                player->decoder_initialized = MA_TRUE;
                player->sample_rate = player->decoder.outputSampleRate;
                player->channels = player->decoder.outputChannels;
                ma_decoder_get_length_in_pcm_frames(&player->decoder, &player->length_in_samples);
                strcpy(player->format_str, "FFmpeg");
                success = MA_TRUE;
            }
        }
    }

    napi_value result;
    napi_get_boolean(env, success, &result);
    return result;
}

static napi_value Juce_openStream(napi_env env, napi_callback_info info) {
    size_t argc = 4;
    napi_value args[4];
    napi_value thiz;
    napi_get_cb_info(env, info, &argc, args, &thiz, NULL);

    PlayerInstance* player;
    napi_unwrap(env, thiz, (void**)&player);

    void* dataPtr = NULL;
    size_t dataLen = 0;
    if (argc > 1 && args[1] != NULL) {
        napi_valuetype vtype;
        napi_typeof(env, args[1], &vtype);
        if (vtype == napi_object) {
            napi_get_buffer_info(env, args[1], &dataPtr, &dataLen);
        }
    }

    ma_bool32 is_next = MA_FALSE;
    if (argc > 2 && args[2] != NULL) {
        napi_valuetype vtype;
        napi_typeof(env, args[2], &vtype);
        if (vtype == napi_boolean) {
            bool b = false;
            napi_get_value_bool(env, args[2], &b);
            is_next = b ? MA_TRUE : MA_FALSE;
        }
    }

    double sampleCount = 0;
    if (argc > 3 && args[3] != NULL) {
        napi_get_value_double(env, args[3], &sampleCount);
    }

    ma_bool32 success = MA_FALSE;
    if (player && dataPtr && dataLen > 0) {
        if (is_next) {
            if (player->next_sound_initialized) {
                ma_sound_stop(&player->next_sound);
                ma_sound_uninit(&player->next_sound);
                player->next_sound_initialized = MA_FALSE;
            }
            if (player->next_decoder_initialized) {
                ma_decoder_uninit(&player->next_decoder);
                player->next_decoder_initialized = MA_FALSE;
            }
            if (player->next_stream_buffer_ref) {
                napi_delete_reference(env, player->next_stream_buffer_ref);
                player->next_stream_buffer_ref = NULL;
            }

            if (ma_decoder_init_memory(dataPtr, dataLen, NULL, &player->next_decoder) == MA_SUCCESS) {
                player->next_decoder_initialized = MA_TRUE;
                ma_decoder_get_length_in_pcm_frames(&player->next_decoder, &player->next_length_in_samples);
                if (player->next_length_in_samples == 0 && sampleCount > 0) {
                    player->next_length_in_samples = (ma_uint64)sampleCount;
                }
                strcpy(player->next_format_str, "FFmpeg");

                napi_create_reference(env, args[1], 1, &player->next_stream_buffer_ref);

                if (player->engine_initialized) {
                    if (ma_sound_init_from_data_source(&player->engine, &player->next_decoder, 0, NULL, &player->next_sound) == MA_SUCCESS) {
                        player->next_sound_initialized = MA_TRUE;
                    }
                }
                success = MA_TRUE;
            }
        } else {
            if (player->sound_initialized) {
                ma_sound_stop(&player->sound);
                ma_sound_uninit(&player->sound);
                player->sound_initialized = MA_FALSE;
            }
            if (player->decoder_initialized) {
                ma_decoder_uninit(&player->decoder);
                player->decoder_initialized = MA_FALSE;
            }
            if (player->stream_buffer_ref) {
                napi_delete_reference(env, player->stream_buffer_ref);
                player->stream_buffer_ref = NULL;
            }

            if (ma_decoder_init_memory(dataPtr, dataLen, NULL, &player->decoder) == MA_SUCCESS) {
                player->decoder_initialized = MA_TRUE;
                player->sample_rate = player->decoder.outputSampleRate;
                player->channels = player->decoder.outputChannels;
                ma_decoder_get_length_in_pcm_frames(&player->decoder, &player->length_in_samples);
                if (player->length_in_samples == 0 && sampleCount > 0) {
                    player->length_in_samples = (ma_uint64)sampleCount;
                }
                strcpy(player->format_str, "FFmpeg");

                napi_create_reference(env, args[1], 1, &player->stream_buffer_ref);

                if (player->engine_initialized) {
                    if (ma_sound_init_from_data_source(&player->engine, &player->decoder, 0, NULL, &player->sound) == MA_SUCCESS) {
                        player->sound_initialized = MA_TRUE;
                    }
                }
                success = MA_TRUE;
            }
        }
    }

    napi_value result;
    napi_get_boolean(env, success, &result);
    return result;
}

static napi_value Juce_close(napi_env env, napi_callback_info info) {
    napi_value thiz;
    napi_get_cb_info(env, info, NULL, NULL, &thiz, NULL);
    PlayerInstance* player;
    napi_unwrap(env, thiz, (void**)&player);

    if (player) {
        if (player->sound_initialized) {
            ma_sound_stop(&player->sound);
            ma_sound_uninit(&player->sound);
            player->sound_initialized = MA_FALSE;
        }
        if (player->decoder_initialized) {
            ma_decoder_uninit(&player->decoder);
            player->decoder_initialized = MA_FALSE;
        }
        if (player->stream_buffer_ref) {
            napi_delete_reference(env, player->stream_buffer_ref);
            player->stream_buffer_ref = NULL;
        }
        if (player->next_sound_initialized) {
            ma_sound_stop(&player->next_sound);
            ma_sound_uninit(&player->next_sound);
            player->next_sound_initialized = MA_FALSE;
        }
        if (player->next_decoder_initialized) {
            ma_decoder_uninit(&player->next_decoder);
            player->next_decoder_initialized = MA_FALSE;
        }
        if (player->next_stream_buffer_ref) {
            napi_delete_reference(env, player->next_stream_buffer_ref);
            player->next_stream_buffer_ref = NULL;
        }
        player->status = 0;
    }

    napi_value undefined;
    napi_get_undefined(env, &undefined);
    return undefined;
}

static napi_value Juce_play(napi_env env, napi_callback_info info) {
    napi_value thiz;
    napi_get_cb_info(env, info, NULL, NULL, &thiz, NULL);
    PlayerInstance* player;
    napi_unwrap(env, thiz, (void**)&player);

    if (player && player->sound_initialized) {
        ma_sound_start(&player->sound);
        player->status = 1; // Playing
    } else if (player && player->decoder_initialized && player->engine_initialized) {
        if (ma_sound_init_from_data_source(&player->engine, &player->decoder, 0, NULL, &player->sound) == MA_SUCCESS) {
            player->sound_initialized = MA_TRUE;
            ma_sound_set_volume(&player->sound, (float)(player->volume / 100.0));
            ma_sound_start(&player->sound);
            player->status = 1;
        }
    }

    napi_value result;
    napi_create_string_utf8(env, "", NAPI_AUTO_LENGTH, &result);
    return result;
}

static napi_value Juce_pause(napi_env env, napi_callback_info info) {
    napi_value thiz;
    napi_get_cb_info(env, info, NULL, NULL, &thiz, NULL);
    PlayerInstance* player;
    napi_unwrap(env, thiz, (void**)&player);

    if (player && player->sound_initialized) {
        ma_sound_stop(&player->sound);
        player->status = 2; // Paused
    }

    napi_value undefined;
    napi_get_undefined(env, &undefined);
    return undefined;
}

static napi_value Juce_status(napi_env env, napi_callback_info info) {
    napi_value thiz;
    napi_get_cb_info(env, info, NULL, NULL, &thiz, NULL);
    PlayerInstance* player;
    napi_unwrap(env, thiz, (void**)&player);

    const char* status_str = "Stopped";
    if (player) {
        if (player->status == 1) status_str = "Playing";
        else if (player->status == 2) status_str = "Paused";
        else status_str = "Stopped";
    }

    napi_value result;
    napi_create_string_utf8(env, status_str, NAPI_AUTO_LENGTH, &result);
    return result;
}

static napi_value Juce_volume(napi_env env, napi_callback_info info) {
    size_t argc = 1;
    napi_value args[1];
    napi_value thiz;
    napi_get_cb_info(env, info, &argc, args, &thiz, NULL);

    PlayerInstance* player;
    napi_unwrap(env, thiz, (void**)&player);

    if (argc > 0 && args[0] != NULL) {
        double vol = 100;
        napi_get_value_double(env, args[0], &vol);
        if (player) {
            player->volume = (float)vol;
            if (player->sound_initialized) {
                ma_sound_set_volume(&player->sound, (float)(vol / 100.0));
            }
        }
        napi_value undefined;
        napi_get_undefined(env, &undefined);
        return undefined;
    } else {
        napi_value result;
        napi_create_double(env, player ? player->volume : 100.0, &result);
        return result;
    }
}

static napi_value Juce_offset(napi_env env, napi_callback_info info) {
    size_t argc = 1;
    napi_value args[1];
    napi_value thiz;
    napi_get_cb_info(env, info, &argc, args, &thiz, NULL);

    PlayerInstance* player;
    napi_unwrap(env, thiz, (void**)&player);

    if (argc > 0 && args[0] != NULL) {
        double seconds = 0;
        napi_valuetype vtype;
        napi_typeof(env, args[0], &vtype);
        if (vtype != napi_undefined && vtype != napi_null) {
            napi_get_value_double(env, args[0], &seconds);
            if (player && player->sound_initialized && player->sample_rate > 0) {
                ma_uint64 frame_idx = (ma_uint64)(seconds * player->sample_rate);
                ma_sound_seek_to_pcm_frame(&player->sound, frame_idx);
            }
        }
    }

    double current_seconds = 0;
    if (player && player->sound_initialized && player->sample_rate > 0) {
        ma_uint64 cursor = 0;
        ma_sound_get_cursor_in_pcm_frames(&player->sound, &cursor);
        current_seconds = (double)cursor / player->sample_rate;
    }

    napi_value result;
    napi_create_double(env, current_seconds, &result);
    return result;
}

// --- Device Functions ---

static napi_value Juce_getAvailableDeviceList(napi_env env, napi_callback_info info) {
    napi_value arr;
    napi_create_array(env, &arr);

    ma_context context;
    if (ma_context_init(NULL, 0, NULL, &context) == MA_SUCCESS) {
        ma_device_info* pPlaybackDeviceInfos;
        ma_uint32 playbackDeviceCount;
        if (ma_context_get_devices(&context, &pPlaybackDeviceInfos, &playbackDeviceCount, NULL, NULL) == MA_SUCCESS) {
            for (ma_uint32 i = 0; i < playbackDeviceCount; i++) {
                napi_value item;
                napi_create_object(env, &item);

                napi_value vName, vType;
                napi_create_string_utf8(env, pPlaybackDeviceInfos[i].name, NAPI_AUTO_LENGTH, &vName);

                const char* backendName = "Audio";
                switch (context.backend) {
                    case ma_backend_wasapi: backendName = "WASAPI"; break;
                    case ma_backend_dsound: backendName = "DirectSound"; break;
                    case ma_backend_winmm: backendName = "WinMM"; break;
                    case ma_backend_coreaudio: backendName = "CoreAudio"; break;
                    case ma_backend_alsa: backendName = "ALSA"; break;
                    case ma_backend_pulseaudio: backendName = "PulseAudio"; break;
                    case ma_backend_jack: backendName = "JACK"; break;
                    default: backendName = "miniaudio"; break;
                }
                napi_create_string_utf8(env, backendName, NAPI_AUTO_LENGTH, &vType);

                napi_set_named_property(env, item, "name", vName);
                napi_set_named_property(env, item, "type", vType);

                napi_set_element(env, arr, i, item);
            }
        }
        ma_context_uninit(&context);
    } else {
        printf("Failed to create context!");
    }

    return arr;
}

static napi_value Juce_useDevice(napi_env env, napi_callback_info info) {
    size_t argc = 2;
    napi_value args[2];
    napi_value thiz;
    napi_get_cb_info(env, info, &argc, args, &thiz, NULL);

    PlayerInstance* player;
    napi_unwrap(env, thiz, (void**)&player);

    if (player) {
        if (argc > 0 && args[0] != NULL) {
            size_t len = 0;
            napi_get_value_string_utf8(env, args[0], player->device_name, sizeof(player->device_name), &len);
        }
        if (argc > 1 && args[1] != NULL) {
            size_t len = 0;
            napi_get_value_string_utf8(env, args[1], player->device_type, sizeof(player->device_type), &len);
        }

        ma_context context;
        if (ma_context_init(NULL, 0, NULL, &context) == MA_SUCCESS) {
            ma_device_info* pPlaybackDeviceInfos;
            ma_uint32 playbackDeviceCount;
            if (ma_context_get_devices(&context, &pPlaybackDeviceInfos, &playbackDeviceCount, NULL, NULL) == MA_SUCCESS) {
                for (ma_uint32 i = 0; i < playbackDeviceCount; i++) {
                    if (strcmp(pPlaybackDeviceInfos[i].name, player->device_name) == 0) {
                        player->playbackDeviceID = pPlaybackDeviceInfos[i].id;
                        player->hasDeviceID = MA_TRUE;
                        break;
                    }
                }
            }
            ma_context_uninit(&context);
        }

        if (player->engine_initialized) {
            if (player->sound_initialized) {
                ma_sound_stop(&player->sound);
                ma_sound_uninit(&player->sound);
                player->sound_initialized = MA_FALSE;
            }
            ma_engine_uninit(&player->engine);
            player->engine_initialized = MA_FALSE;
        }

        ma_engine_config engineConfig = ma_engine_config_init();
        if (player->hasDeviceID) {
            engineConfig.pPlaybackDeviceID = &player->playbackDeviceID;
        }

        if (ma_engine_init(&engineConfig, &player->engine) == MA_SUCCESS) {
            player->engine_initialized = MA_TRUE;
        }

        if (player->engine_initialized && player->decoder_initialized) {
            if (ma_sound_init_from_data_source(&player->engine, &player->decoder, 0, NULL, &player->sound) == MA_SUCCESS) {
                player->sound_initialized = MA_TRUE;
                ma_sound_set_volume(&player->sound, (float)(player->volume / 100.0));
            }
        }
    }

    napi_value result;
    napi_create_string_utf8(env, "", NAPI_AUTO_LENGTH, &result);
    return result;
}

static napi_value Juce_deviceInformation(napi_env env, napi_callback_info info) {
    napi_value thiz;
    napi_get_cb_info(env, info, NULL, NULL, &thiz, NULL);
    PlayerInstance* player;
    napi_unwrap(env, thiz, (void**)&player);

    napi_value obj;
    napi_create_object(env, &obj);

    const char* name = (player && player->device_name[0]) ? player->device_name : "";
    const char* type = (player && player->device_type[0]) ? player->device_type : "";

    if (!name[0]) {
        ma_context context;
        if (ma_context_init(NULL, 0, NULL, &context) == MA_SUCCESS) {
            ma_device_info* pPlaybackDeviceInfos;
            ma_uint32 playbackDeviceCount;
            if (ma_context_get_devices(&context, &pPlaybackDeviceInfos, &playbackDeviceCount, NULL, NULL) == MA_SUCCESS) {
                if (playbackDeviceCount > 0) {
                    name = pPlaybackDeviceInfos[0].name;
                    type = "Default";
                }
            }
            ma_context_uninit(&context);
        }
    }

    napi_value vName, vType;
    napi_create_string_utf8(env, name, NAPI_AUTO_LENGTH, &vName);
    napi_create_string_utf8(env, type, NAPI_AUTO_LENGTH, &vType);

    napi_set_named_property(env, obj, "name", vName);
    napi_set_named_property(env, obj, "type", vType);

    return obj;
}

// --- Standard Methods ---

static napi_value Juce_trackInformation(napi_env env, napi_callback_info info) {
    napi_value thiz;
    napi_get_cb_info(env, info, NULL, NULL, &thiz, NULL);
    PlayerInstance* player;
    napi_unwrap(env, thiz, (void**)&player);

    napi_value obj;
    napi_create_object(env, &obj);

    napi_value vFormat, vChannel, vSampleRate, vBits, vLen, vDuration;

    const char* fmt = (player && player->format_str[0]) ? player->format_str : "FFmpeg";
    ma_uint32 ch = player ? player->channels : 2;
    ma_uint32 sr = player ? player->sample_rate : 44100;
    ma_uint64 lenSamples = player ? player->length_in_samples : 0;
    double durationMs = (sr > 0) ? ((double)lenSamples * 1000.0 / sr) : 0;

    napi_create_string_utf8(env, fmt, NAPI_AUTO_LENGTH, &vFormat);
    napi_create_uint32(env, ch, &vChannel);
    napi_create_uint32(env, sr, &vSampleRate);
    napi_create_uint32(env, 16, &vBits);
    napi_create_double(env, (double)lenSamples, &vLen);
    napi_create_double(env, durationMs, &vDuration);

    napi_set_named_property(env, obj, "format", vFormat);
    napi_set_named_property(env, obj, "channel", vChannel);
    napi_set_named_property(env, obj, "sampleRate", vSampleRate);
    napi_set_named_property(env, obj, "bitsPerSample", vBits);
    napi_set_named_property(env, obj, "lengthInSamples", vLen);
    napi_set_named_property(env, obj, "duration", vDuration);

    return obj;
}

static napi_value Juce_hasStreamFinished(napi_env env, napi_callback_info info) {
    napi_value thiz;
    napi_get_cb_info(env, info, NULL, NULL, &thiz, NULL);
    PlayerInstance* player;
    napi_unwrap(env, thiz, (void**)&player);

    ma_bool32 finished = MA_FALSE;
    if (player && player->sound_initialized) {
        finished = ma_sound_at_end(&player->sound);
    }

    napi_value result;
    napi_get_boolean(env, finished, &result);
    return result;
}

static napi_value Juce_initCacheRange(napi_env env, napi_callback_info info) {
    size_t argc = 3;
    napi_value args[3];
    napi_value thiz;
    napi_get_cb_info(env, info, &argc, args, &thiz, NULL);

    PlayerInstance* player;
    napi_unwrap(env, thiz, (void**)&player);

    ma_uint32 sr = player ? player->sample_rate : 44100;
    ma_uint64 lenSamples = player ? player->length_in_samples : 0;
    double durationMs = (sr > 0) ? ((double)lenSamples * 1000.0 / sr) : 0;

    if (argc >= 2 && args[1] != NULL) {
        CacheRangeObj* cache_range = NULL;
        napi_status status = napi_unwrap(env, args[1], (void**)&cache_range);
        if (status == napi_ok && cache_range != NULL) {
            cache_range->cache_size = durationMs / 1000;
        }
    }

    napi_value undefined;
    napi_get_undefined(env, &undefined);
    return undefined;
}

static napi_value Juce_releaseResources(napi_env env, napi_callback_info info) {
    napi_value thiz;
    napi_get_cb_info(env, info, NULL, NULL, &thiz, NULL);
    PlayerInstance* player;
    napi_unwrap(env, thiz, (void**)&player);

    if (player) {
        if (player->sound_initialized) {
            ma_sound_stop(&player->sound);
            ma_sound_uninit(&player->sound);
            player->sound_initialized = MA_FALSE;
        }
        if (player->decoder_initialized) {
            ma_decoder_uninit(&player->decoder);
            player->decoder_initialized = MA_FALSE;
        }
        if (player->stream_buffer_ref) {
            napi_delete_reference(env, player->stream_buffer_ref);
            player->stream_buffer_ref = NULL;
        }
    }

    napi_value undefined;
    napi_get_undefined(env, &undefined);
    return undefined;
}

static napi_value Juce_lookAheadInSeconds(napi_env env, napi_callback_info info) {
    size_t argc = 1;
    napi_value args[1];
    napi_value thiz;
    napi_get_cb_info(env, info, &argc, args, &thiz, NULL);

    PlayerInstance* player;
    napi_unwrap(env, thiz, (void**)&player);

    if (argc > 0 && args[0] != NULL) {
        double secs = 0;
        napi_get_value_double(env, args[0], &secs);
        if (player) player->look_ahead = secs;
    }

    napi_value undefined;
    napi_get_undefined(env, &undefined);
    return undefined;
}

static napi_value Juce_setDeviceChangedListener(napi_env env, napi_callback_info info) {
    size_t argc = 1;
    napi_value args[1];
    napi_value thiz;
    napi_get_cb_info(env, info, &argc, args, &thiz, NULL);

    PlayerInstance* player;
    napi_unwrap(env, thiz, (void**)&player);

    if (player && argc > 0 && args[0] != NULL) {
        if (player->device_changed_listener_ref != NULL) {
            napi_delete_reference(env, player->device_changed_listener_ref);
            player->device_changed_listener_ref = NULL;
        }
        napi_create_reference(env, args[0], 1, &player->device_changed_listener_ref);
        player->env = env;
    }

    napi_value undefined;
    napi_get_undefined(env, &undefined);
    return undefined;
}

static napi_value Juce_clearNextTrack(napi_env env, napi_callback_info info) {
    napi_value thiz;
    napi_get_cb_info(env, info, NULL, NULL, &thiz, NULL);
    PlayerInstance* player;
    napi_unwrap(env, thiz, (void**)&player);

    if (player) {
        if (player->next_sound_initialized) {
            ma_sound_stop(&player->next_sound);
            ma_sound_uninit(&player->next_sound);
            player->next_sound_initialized = MA_FALSE;
        }
        if (player->next_decoder_initialized) {
            ma_decoder_uninit(&player->next_decoder);
            player->next_decoder_initialized = MA_FALSE;
        }
        if (player->next_stream_buffer_ref) {
            napi_delete_reference(env, player->next_stream_buffer_ref);
            player->next_stream_buffer_ref = NULL;
        }
        player->next_length_in_samples = 0;
        player->next_format_str[0] = '\0';
    }

    napi_value undefined;
    napi_get_undefined(env, &undefined);
    return undefined;
}

static napi_value Juce_switchToNext(napi_env env, napi_callback_info info) {
    napi_value thiz;
    napi_get_cb_info(env, info, NULL, NULL, &thiz, NULL);
    PlayerInstance* player;
    napi_unwrap(env, thiz, (void**)&player);

    ma_bool32 success = MA_FALSE;
    if (player && player->next_decoder_initialized) {
        if (player->sound_initialized) {
            ma_sound_stop(&player->sound);
            ma_sound_uninit(&player->sound);
            player->sound_initialized = MA_FALSE;
        }
        if (player->decoder_initialized) {
            ma_decoder_uninit(&player->decoder);
            player->decoder_initialized = MA_FALSE;
        }
        if (player->stream_buffer_ref) {
            napi_delete_reference(env, player->stream_buffer_ref);
            player->stream_buffer_ref = NULL;
        }

        player->decoder = player->next_decoder;
        player->decoder_initialized = player->next_decoder_initialized;
        player->sound = player->next_sound;
        player->sound_initialized = player->next_sound_initialized;
        player->stream_buffer_ref = player->next_stream_buffer_ref;
        player->length_in_samples = player->next_length_in_samples;
        strcpy(player->format_str, player->next_format_str);

        player->sample_rate = player->decoder.outputSampleRate;
        player->channels = player->decoder.outputChannels;

        memset(&player->next_decoder, 0, sizeof(ma_decoder));
        memset(&player->next_sound, 0, sizeof(ma_sound));
        player->next_decoder_initialized = MA_FALSE;
        player->next_sound_initialized = MA_FALSE;
        player->next_stream_buffer_ref = NULL;
        player->next_length_in_samples = 0;
        player->next_format_str[0] = '\0';

        if (player->sound_initialized) {
            ma_sound_set_volume(&player->sound, (float)(player->volume / 100.0));
            if (player->status == 1) {
                ma_sound_start(&player->sound);
            }
        } else if (player->decoder_initialized && player->engine_initialized) {
            if (ma_sound_init_from_data_source(&player->engine, &player->decoder, 0, NULL, &player->sound) == MA_SUCCESS) {
                player->sound_initialized = MA_TRUE;
                ma_sound_set_volume(&player->sound, (float)(player->volume / 100.0));
                if (player->status == 1) {
                    ma_sound_start(&player->sound);
                }
            }
        }

        success = MA_TRUE;
    }

    napi_value result;
    napi_get_boolean(env, success, &result);
    return result;
}

static napi_value Juce_newCacheRanges(napi_env env, napi_callback_info info) {
    CacheRangeObj* cacheObj = (CacheRangeObj*)malloc(sizeof(CacheRangeObj));
    if (!cacheObj) return NULL;
    memset(cacheObj, 0, sizeof(CacheRangeObj));
    
    cacheObj->capacity = 16;
    cacheObj->intervals = (CacheInterval*)malloc(cacheObj->capacity * sizeof(CacheInterval));
    if (!cacheObj->intervals) {
        free(cacheObj);
        return NULL;
    }

    napi_value obj;
    napi_create_object(env, &obj);
    napi_wrap(env, obj, cacheObj, CacheRange_finalize, NULL, NULL);

    napi_value fn;
    #define BIND_METHOD(name, cb) \
        napi_create_function(env, name, NAPI_AUTO_LENGTH, cb, NULL, &fn); \
        napi_set_named_property(env, obj, name, fn);

    BIND_METHOD("addRange", CacheRange_addRange)
    BIND_METHOD("contains", CacheRange_contains)
    BIND_METHOD("getRangeCopy", CacheRange_getRangeCopy)
    BIND_METHOD("isComplete", CacheRange_isComplete)
    BIND_METHOD("isEmpty", CacheRange_isEmpty)
    BIND_METHOD("setCacheSize", CacheRange_setCacheSize)

    return obj;
}

// --- Module Initialization Factory ---

static napi_value CreateInnerObject(napi_env env, napi_callback_info info) {
    PlayerInstance* player = (PlayerInstance*)malloc(sizeof(PlayerInstance));
    if (!player) return NULL;
    memset(player, 0, sizeof(PlayerInstance));
    player->volume = 100.0f;

    ma_engine_config engineConfig = ma_engine_config_init();
    if (ma_engine_init(&engineConfig, &player->engine) == MA_SUCCESS) {
        player->engine_initialized = MA_TRUE;
    }

    napi_value obj;
    napi_create_object(env, &obj);
    napi_wrap(env, obj, player, PlayerInstance_finalize, NULL, NULL);

    #define BIND_JUCE_METHOD(name, cb) \
        napi_value fn_##name; \
        napi_create_function(env, #name, NAPI_AUTO_LENGTH, cb, NULL, &fn_##name); \
        napi_set_named_property(env, obj, #name, fn_##name);

    BIND_JUCE_METHOD(open, Juce_open)
    BIND_JUCE_METHOD(openStream, Juce_openStream)
    BIND_JUCE_METHOD(close, Juce_close)
    BIND_JUCE_METHOD(play, Juce_play)
    BIND_JUCE_METHOD(pause, Juce_pause)
    BIND_JUCE_METHOD(status, Juce_status)
    BIND_JUCE_METHOD(volume, Juce_volume)
    BIND_JUCE_METHOD(offset, Juce_offset)
    BIND_JUCE_METHOD(getAvailableDeviceList, Juce_getAvailableDeviceList)
    BIND_JUCE_METHOD(useDevice, Juce_useDevice)
    BIND_JUCE_METHOD(deviceInformation, Juce_deviceInformation)
    BIND_JUCE_METHOD(trackInformation, Juce_trackInformation)
    BIND_JUCE_METHOD(hasStreamFinished, Juce_hasStreamFinished)
    BIND_JUCE_METHOD(initCacheRange, Juce_initCacheRange)
    BIND_JUCE_METHOD(releaseResources, Juce_releaseResources)
    BIND_JUCE_METHOD(lookAheadInSeconds, Juce_lookAheadInSeconds)
    BIND_JUCE_METHOD(setDeviceChangedListener, Juce_setDeviceChangedListener)
    BIND_JUCE_METHOD(clearNextTrack, Juce_clearNextTrack)
    BIND_JUCE_METHOD(switchToNext, Juce_switchToNext)
    BIND_JUCE_METHOD(newCacheRanges, Juce_newCacheRanges)

    return obj;
}

static napi_value Init(napi_env env, napi_value exports) {
    napi_value factory_fn;
    napi_create_function(env, "exports", NAPI_AUTO_LENGTH, CreateInnerObject, NULL, &factory_fn);
    return factory_fn;
}

NAPI_MODULE(NODE_GYP_MODULE_NAME, Init)