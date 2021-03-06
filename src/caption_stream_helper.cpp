/******************************************************************************
Copyright (C) 2019 by <rat.with.a.compiler@gmail.com>

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.
******************************************************************************/

#include <QComboBox>

#include <CaptionStream.h>
#include "log.c"
#include <utils.h>
#include "storage_utils.h"
#include "CaptionPluginSettings.h"
#include <util/util.hpp>
#include <util/platform.h>

#define SAVE_ENTRY_NAME "cloud_closed_caption_rat"

static CaptionStreamSettings default_CaptionStreamSettings() {
    uint download_start_delay_ms = 4000;
    download_start_delay_ms = 40;
    return {
            5000,
            5000,
            180'000,
            50,
            download_start_delay_ms,
            "en-US",
            0,
            ""
    };
};


static ContinuousCaptionStreamSettings default_ContinuousCaptionStreamSettings() {
    return {
            280,
            5,
            10,
            default_CaptionStreamSettings()
    };
};

static CaptionFormatSettings default_CaptionFormatSettings() {
    return {
            32,
            3,
            false,
            {"niger", "nigger", "nigga", "niggas", "fag", "faggot", "chink"}, // default banned words
            true,
            25.0,
    };
};


static CaptionSourceSettings default_CaptionSourceSettings() {
    return {
            "",
            CAPTION_SOURCE_MUTE_TYPE_FROM_OWN_SOURCE,
            ""
    };
}

static SourceCaptionerSettings default_SourceCaptionerSettings() {
    return SourceCaptionerSettings(
            true,
            false,
            default_CaptionSourceSettings(),
            default_CaptionFormatSettings(),
            default_ContinuousCaptionStreamSettings()
    );
};

static CaptionPluginSettings default_CaptionPluginSettings() {
    return CaptionPluginSettings(
            false,
            default_SourceCaptionerSettings()
    );
}


static void enforce_sensible_values(CaptionPluginSettings &settings) {
    SourceCaptionerSettings &source_settings = settings.source_cap_settings;
    if (source_settings.format_settings.caption_line_count <= 0 || source_settings.format_settings.caption_line_count > 4)
        source_settings.format_settings.caption_line_count = 1;

    // backwards compatibility with old ettings that had off, mild, strict instead off/on.
    // ensure old strict/2 falls back to on/1 not off/0 default.
    if (source_settings.stream_settings.stream_settings.profanity_filter == 2)
        source_settings.stream_settings.stream_settings.profanity_filter = 1;
}

static string current_scene_collection_name() {
    string name;

    char *scene_collection_name = obs_frontend_get_current_scene_collection();
    if (scene_collection_name)
        name = scene_collection_name;
    bfree(scene_collection_name);

    return name;
}

static CaptionPluginSettings get_CaptionPluginSettings_from_data(obs_data_t *load_data) {
    auto settings = default_CaptionPluginSettings();
    SourceCaptionerSettings &source_settings = settings.source_cap_settings;

    source_settings.print();
    if (!load_data) {
        info_log("no data, using default CaptionPluginSettings");
    } else {
        obs_data_set_default_bool(load_data, "enabled", settings.enabled);

        obs_data_set_default_bool(load_data, "streaming_output_enabled", source_settings.streaming_output_enabled);
        obs_data_set_default_bool(load_data, "recording_output_enabled", source_settings.recording_output_enabled);
        obs_data_set_default_bool(load_data, "caption_insert_newlines", source_settings.format_settings.caption_insert_newlines);
        obs_data_set_default_int(load_data, "caption_line_count", source_settings.format_settings.caption_line_count);
        obs_data_set_default_string(load_data, "manual_banned_words", "");
        obs_data_set_default_string(load_data, "mute_source_name", "");
        obs_data_set_default_string(load_data, "source_caption_when", "");

        obs_data_set_default_string(load_data, "source_language", source_settings.stream_settings.stream_settings.language.c_str());
        obs_data_set_default_int(load_data, "profanity_filter", source_settings.stream_settings.stream_settings.profanity_filter);
        obs_data_set_default_string(load_data, "custom_api_key", source_settings.stream_settings.stream_settings.api_key.c_str());

        obs_data_set_default_double(load_data, "caption_timeout_secs", source_settings.format_settings.caption_timeout_seconds);
        obs_data_set_default_bool(load_data, "caption_timeout_enabled", source_settings.format_settings.caption_timeout_enabled);


        settings.enabled = obs_data_get_bool(load_data, "enabled");
        source_settings.streaming_output_enabled = obs_data_get_bool(load_data, "streaming_output_enabled");
        source_settings.recording_output_enabled = obs_data_get_bool(load_data, "recording_output_enabled");

        source_settings.format_settings.caption_insert_newlines = obs_data_get_bool(load_data, "caption_insert_newlines");
        source_settings.format_settings.caption_line_count = (int) obs_data_get_int(load_data, "caption_line_count");

        source_settings.stream_settings.stream_settings.language = obs_data_get_string(load_data, "source_language");
        source_settings.stream_settings.stream_settings.profanity_filter = (int) obs_data_get_int(load_data, "profanity_filter");
        source_settings.stream_settings.stream_settings.api_key = obs_data_get_string(load_data, "custom_api_key");

        source_settings.format_settings.caption_timeout_enabled = obs_data_get_bool(load_data, "caption_timeout_enabled");
        source_settings.format_settings.caption_timeout_seconds = obs_data_get_double(load_data, "caption_timeout_secs");

        string banned_words_line = obs_data_get_string(load_data, "manual_banned_words");
        source_settings.format_settings.manual_banned_words = string_to_banned_words(banned_words_line);

        obs_data_array_t *scene_collection_sources_array = obs_data_get_array(load_data, "scene_collection_sources");

        CaptionSourceSettings default_caption_source_settings = default_CaptionSourceSettings();
        for (size_t index = 0; index < obs_data_array_count(scene_collection_sources_array); index++) {
            obs_data_t *a_scene_source_obj = obs_data_array_item(scene_collection_sources_array, index);

            string scene_collection_name = obs_data_get_string(a_scene_source_obj, "scene_collection_name");
            if (!scene_collection_name.empty()) {
                CaptionSourceSettings caption_source_settings;

                obs_data_set_default_string(a_scene_source_obj, "source_name", default_caption_source_settings.caption_source_name.c_str());
                caption_source_settings.caption_source_name = obs_data_get_string(a_scene_source_obj, "source_name");
                caption_source_settings.mute_source_name = obs_data_get_string(a_scene_source_obj, "mute_source_name");

                string mute_when_str = obs_data_get_string(a_scene_source_obj, "source_caption_when");
                caption_source_settings.mute_when = string_to_mute_setting(mute_when_str, CAPTION_SOURCE_MUTE_TYPE_FROM_OWN_SOURCE);

                source_settings.caption_source_settings_map[scene_collection_name] = caption_source_settings;
            }
            obs_data_release(a_scene_source_obj);
        }
        obs_data_array_release(scene_collection_sources_array);

        enforce_sensible_values(settings);
    }
    source_settings.print();
    return settings;
}

static void set_CaptionPluginSettings_on_data(obs_data_t *save_data, const CaptionPluginSettings &settings) {
    info_log("obs save event");
    const SourceCaptionerSettings &source_settings = settings.source_cap_settings;

    obs_data_set_bool(save_data, "enabled", settings.enabled);
    obs_data_set_bool(save_data, "streaming_output_enabled", source_settings.streaming_output_enabled);
    obs_data_set_bool(save_data, "recording_output_enabled", source_settings.recording_output_enabled);

    obs_data_set_int(save_data, "caption_line_count", source_settings.format_settings.caption_line_count);
    obs_data_set_bool(save_data, "caption_insert_newlines", source_settings.format_settings.caption_insert_newlines);
//    obs_data_set_bool(save_data, "caption_insert_newlines", settings.format_settings.caption_insert_newlines);

    obs_data_set_string(save_data, "source_language", source_settings.stream_settings.stream_settings.language.c_str());
    obs_data_set_int(save_data, "profanity_filter", source_settings.stream_settings.stream_settings.profanity_filter);
    obs_data_set_string(save_data, "custom_api_key", source_settings.stream_settings.stream_settings.api_key.c_str());

    obs_data_set_bool(save_data, "caption_timeout_enabled", source_settings.format_settings.caption_timeout_enabled);
    obs_data_set_double(save_data, "caption_timeout_secs", source_settings.format_settings.caption_timeout_seconds);

    string banned_words_line;
    if (!source_settings.format_settings.manual_banned_words.empty()) {
//        info_log("savingggggg %d", settings.format_settings.manual_banned_words.size());
        words_to_string(source_settings.format_settings.manual_banned_words, banned_words_line);
    }
    obs_data_set_string(save_data, "manual_banned_words", banned_words_line.c_str());


//    obs_data_t *scene_collection_sources_obj = obs_data_create();
    obs_data_array_t *scene_collection_sources_array = obs_data_array_create();
    // TODO: not sure how to enumerate obj keys, so using array for now ...

    for (auto it = source_settings.caption_source_settings_map.begin(); it != source_settings.caption_source_settings_map.end(); ++it) {
        obs_data_t *a_scene_source_obj = obs_data_create();

        string scene_collection_name = it->first;
        CaptionSourceSettings caption_source_settings = it->second;

        obs_data_set_string(a_scene_source_obj, "scene_collection_name", scene_collection_name.c_str());

        string caption_when = mute_setting_to_string(caption_source_settings.mute_when, "");
        obs_data_set_string(a_scene_source_obj, "source_caption_when", caption_when.c_str());

        obs_data_set_string(a_scene_source_obj, "source_name", caption_source_settings.caption_source_name.c_str());
        obs_data_set_string(a_scene_source_obj, "mute_source_name", caption_source_settings.mute_source_name.c_str());

        obs_data_array_push_back(scene_collection_sources_array, a_scene_source_obj);
//        obs_data_set_obj(scene_collection_sources_obj, scene_collection_name.c_str(), a_scene_source_obj);
        obs_data_release(a_scene_source_obj);
    }

//    obs_data_set_obj(save_data, "scene_collection_sources", scene_collection_sources_obj);
//    obs_data_release(scene_collection_sources_obj);
    obs_data_set_array(save_data, "scene_collection_sources", scene_collection_sources_array);
    obs_data_array_release(scene_collection_sources_array);
}

static bool save_plugin_data_to_config(obs_data_t *data) {
    BPtr<char> module_path = obs_module_get_config_path(obs_current_module(), "");
    if (!module_path) {
        error_log("obs_module_config_path failed, couldn't get captions settings dir for save");
        return false;
    }
    os_mkdirs(module_path);

    BPtr<char> config_file_path = obs_module_config_path("cloud_closed_captions_settings.json");
    if (!config_file_path) {
        error_log("obs_module_config_path failed, couldn't get captions settings file for save");
        return false;
    }

    return obs_data_save_json_safe(data, config_file_path, "tmp", "bak");
}

static bool save_CaptionPluginSettings_to_config(const CaptionPluginSettings &settings) {
    obs_data_t *save_data = obs_data_create();
    set_CaptionPluginSettings_on_data(save_data, settings);

    bool ok = save_plugin_data_to_config(save_data);
    if (ok)
        debug_log("google_s2t_caption_plugin saved settings to config file");
    else
        debug_log("google_s2t_caption_plugin config file save failed");

    obs_data_release(save_data);
    return ok;
}

static obs_data_t *load_plugin_data_from_config() {
    BPtr<char> config_file_path = obs_module_config_path("cloud_closed_captions_settings.json");
    if (!config_file_path) {
        error_log("obs_module_config_path failed, couldn't get captions settings file for load");
        return nullptr;
    }

    return obs_data_create_from_json_file_safe(config_file_path, "bak");
}

static CaptionPluginSettings load_CaptionPluginSettings_from_config() {
    obs_data_t *loaded_data = load_plugin_data_from_config();
    if (loaded_data) {
        CaptionPluginSettings settings = get_CaptionPluginSettings_from_data(loaded_data);
        obs_data_release(loaded_data);
        debug_log("google_s2t_caption_plugin loaded settings from config file");
        return settings;
    }

    debug_log("google_s2t_caption_plugin no plugin config file found, using default settings");
    return default_CaptionPluginSettings();
}

static bool is_stream_live() {
    return obs_frontend_streaming_active();
}

static bool is_recording_live() {
    return obs_frontend_recording_active();
}


static void setup_combobox_profanity(QComboBox &comboBox) {
    while (comboBox.count())
        comboBox.removeItem(0);

    comboBox.addItem("Off", 0);
    comboBox.addItem("On (Unreliable!)", 1);
}

static void setup_combobox_output_target(QComboBox &comboBox) {
    while (comboBox.count())
        comboBox.removeItem(0);

    comboBox.addItem("Streams Only", 0);
    comboBox.addItem("Local Recordings Only", 1);
    comboBox.addItem("Streams & Local Recordings", 2);
}


static bool set_streaming_recording_enabled(const int combo_box_data, bool &streaming_enabled, bool &recording_enabled) {
    if (combo_box_data == 0) {
        streaming_enabled = true;
        recording_enabled = false;
        return true;
    }

    if (combo_box_data == 1) {
        streaming_enabled = false;
        recording_enabled = true;
        return true;
    }

    if (combo_box_data == 2) {
        streaming_enabled = true;
        recording_enabled = true;
        return true;
    }

    return false;
}

static void update_combobox_output_target(QComboBox &comboBox, bool streaming_enabled, bool recording_enabled) {
    int index = 0;
    if (streaming_enabled && !recording_enabled)
        index = 0;
    else if (!streaming_enabled && recording_enabled)
        index = 1;
    else if (streaming_enabled && recording_enabled)
        index = 2;

    comboBox.setCurrentIndex(index);
}

static void setup_combobox_languages(QComboBox &comboBox) {
    while (comboBox.count())
        comboBox.removeItem(0);

    comboBox.addItem("English (United States)", "en-US");
    comboBox.addItem("Afrikaans (South Africa)", "af-ZA");
//    comboBox.addItem("Amharic (Ethiopia)", "am-ET");
//    comboBox.addItem("Armenian (Armenia)", "hy-AM");
//    comboBox.addItem("Azerbaijani (Azerbaijan)", "az-AZ");
//    comboBox.addItem("Indonesian (Indonesia)", "id-ID");
//    comboBox.addItem("Malay (Malaysia)", "ms-MY");
//    comboBox.addItem("Bengali (Bangladesh)", "bn-BD");
//    comboBox.addItem("Bengali (India)", "bn-IN");
    comboBox.addItem("Catalan (Spain)", "ca-ES");
    comboBox.addItem("Czech (Czech Republic)", "cs-CZ");
    comboBox.addItem("Danish (Denmark)", "da-DK");
    comboBox.addItem("German (Germany)", "de-DE");
    comboBox.addItem("English (Australia)", "en-AU");
    comboBox.addItem("English (Canada)", "en-CA");
    comboBox.addItem("English (Ghana)", "en-GH");
    comboBox.addItem("English (United Kingdom)", "en-GB");
    comboBox.addItem("English (India)", "en-IN");
    comboBox.addItem("English (Ireland)", "en-IE");
    comboBox.addItem("English (Kenya)", "en-KE");
    comboBox.addItem("English (New Zealand)", "en-NZ");
    comboBox.addItem("English (Nigeria)", "en-NG");
    comboBox.addItem("English (Philippines)", "en-PH");
    comboBox.addItem("English (Singapore)", "en-SG");
    comboBox.addItem("English (South Africa)", "en-ZA");
    comboBox.addItem("English (Tanzania)", "en-TZ");
    comboBox.addItem("Spanish (Argentina)", "es-AR");
    comboBox.addItem("Spanish (Bolivia)", "es-BO");
    comboBox.addItem("Spanish (Chile)", "es-CL");
    comboBox.addItem("Spanish (Colombia)", "es-CO");
    comboBox.addItem("Spanish (Costa Rica)", "es-CR");
    comboBox.addItem("Spanish (Ecuador)", "es-EC");
    comboBox.addItem("Spanish (El Salvador)", "es-SV");
    comboBox.addItem("Spanish (Spain)", "es-ES");
    comboBox.addItem("Spanish (United States)", "es-US");
    comboBox.addItem("Spanish (Guatemala)", "es-GT");
    comboBox.addItem("Spanish (Honduras)", "es-HN");
    comboBox.addItem("Spanish (Mexico)", "es-MX");
    comboBox.addItem("Spanish (Nicaragua)", "es-NI");
    comboBox.addItem("Spanish (Panama)", "es-PA");
    comboBox.addItem("Spanish (Paraguay)", "es-PY");
    comboBox.addItem("Spanish (Peru)", "es-PE");
    comboBox.addItem("Spanish (Puerto Rico)", "es-PR");
    comboBox.addItem("Spanish (Dominican Republic)", "es-DO");
    comboBox.addItem("Spanish (Uruguay)", "es-UY");
    comboBox.addItem("Spanish (Venezuela)", "es-VE");
    comboBox.addItem("Basque (Spain)", "eu-ES");
//    comboBox.addItem("Filipino (Philippines)", "fil-PH");
    comboBox.addItem("French (Canada)", "fr-CA");
    comboBox.addItem("French (France)", "fr-FR");
    comboBox.addItem("Galician (Spain)", "gl-ES");
//    comboBox.addItem("Georgian (Georgia)", "ka-GE");
//    comboBox.addItem("Gujarati (India)", "gu-IN");
    comboBox.addItem("Croatian (Croatia)", "hr-HR");
//    comboBox.addItem("Zulu (South Africa)", "zu-ZA");
    comboBox.addItem("Icelandic (Iceland)", "is-IS");
    comboBox.addItem("Italian (Italy)", "it-IT");
//    comboBox.addItem("Javanese (Indonesia)", "jv-ID");
//    comboBox.addItem("Kannada (India)", "kn-IN");
//    comboBox.addItem("Khmer (Cambodia)", "km-KH");
//    comboBox.addItem("Lao (Laos)", "lo-LA");
    comboBox.addItem("Latvian (Latvia)", "lv-LV");
    comboBox.addItem("Lithuanian (Lithuania)", "lt-LT");
    comboBox.addItem("Hungarian (Hungary)", "hu-HU");
//    comboBox.addItem("Malayalam (India)", "ml-IN");
//    comboBox.addItem("Marathi (India)", "mr-IN");
    comboBox.addItem("Dutch (Netherlands)", "nl-NL");
//    comboBox.addItem("Nepali (Nepal)", "ne-NP");
//    comboBox.addItem("Norwegian Bokm??l (Norway)", "nb-NO");
    comboBox.addItem("Polish (Poland)", "pl-PL");
    comboBox.addItem("Portuguese (Brazil)", "pt-BR");
    comboBox.addItem("Portuguese (Portugal)", "pt-PT");
    comboBox.addItem("Romanian (Romania)", "ro-RO");
//    comboBox.addItem("Sinhala (Sri Lanka)", "si-LK");
    comboBox.addItem("Slovak (Slovakia)", "sk-SK");
    comboBox.addItem("Slovenian (Slovenia)", "sl-SI");
//    comboBox.addItem("Sundanese (Indonesia)", "su-ID");
//    comboBox.addItem("Swahili (Tanzania)", "sw-TZ");
//    comboBox.addItem("Swahili (Kenya)", "sw-KE");
//    comboBox.addItem("Finnish (Finland)", "fi-FI");
//    comboBox.addItem("Swedish (Sweden)", "sv-SE");
//    comboBox.addItem("Tamil (India)", "ta-IN");
//    comboBox.addItem("Tamil (Singapore)", "ta-SG");
//    comboBox.addItem("Tamil (Sri Lanka)", "ta-LK");
//    comboBox.addItem("Tamil (Malaysia)", "ta-MY");
//    comboBox.addItem("Telugu (India)", "te-IN");
//    comboBox.addItem("Vietnamese (Vietnam)", "vi-VN");
//    comboBox.addItem("Turkish (Turkey)", "tr-TR");
//    comboBox.addItem("Urdu (Pakistan)", "ur-PK");
//    comboBox.addItem("Urdu (India)", "ur-IN");
//    comboBox.addItem("Greek (Greece)", "el-GR");
//    comboBox.addItem("Bulgarian (Bulgaria)", "bg-BG");
//    comboBox.addItem("Russian (Russia)", "ru-RU");
//    comboBox.addItem("Serbian (Serbia)", "sr-RS");
//    comboBox.addItem("Ukrainian (Ukraine)", "uk-UA");
//    comboBox.addItem("Hebrew (Israel)", "he-IL");
//    comboBox.addItem("Arabic (Israel)", "ar-IL");
//    comboBox.addItem("Arabic (Jordan)", "ar-JO");
//    comboBox.addItem("Arabic (United Arab Emirates)", "ar-AE");
//    comboBox.addItem("Arabic (Bahrain)", "ar-BH");
//    comboBox.addItem("Arabic (Algeria)", "ar-DZ");
//    comboBox.addItem("Arabic (Saudi Arabia)", "ar-SA");
//    comboBox.addItem("Arabic (Iraq)", "ar-IQ");
//    comboBox.addItem("Arabic (Kuwait)", "ar-KW");
//    comboBox.addItem("Arabic (Morocco)", "ar-MA");
//    comboBox.addItem("Arabic (Tunisia)", "ar-TN");
//    comboBox.addItem("Arabic (Oman)", "ar-OM");
//    comboBox.addItem("Arabic (State of Palestine)", "ar-PS");
//    comboBox.addItem("Arabic (Qatar)", "ar-QA");
//    comboBox.addItem("Arabic (Lebanon)", "ar-LB");
//    comboBox.addItem("Arabic (Egypt)", "ar-EG");
//    comboBox.addItem("Persian (Iran)", "fa-IR");
//    comboBox.addItem("Hindi (India)", "hi-IN");
//    comboBox.addItem("Thai (Thailand)", "th-TH");
//    comboBox.addItem("Korean (South Korea)", "ko-KR");
//    comboBox.addItem("Chinese, Mandarin (Traditional, Taiwan)", "zh-TW");
//    comboBox.addItem("Chinese, Cantonese (Traditional, Hong Kong)", "yue-Hant-HK");
//    comboBox.addItem("Japanese (Japan)", "ja-JP");
//    comboBox.addItem("Chinese, Mandarin (Simplified, Hong Kong)", "zh-HK");
//    comboBox.addItem("Chinese, Mandarin (Simplified, China)", "zh");
}