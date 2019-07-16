#include <memory>


#include "SourceCaptioner.h"
#include "log.c"


SourceCaptioner::SourceCaptioner(CaptionerSettings settings) :
        QObject(),
        settings(settings),
        last_caption_at(std::chrono::steady_clock::now()),
        last_caption_cleared(true) {

    QObject::connect(this, &SourceCaptioner::caption_text_line_received, this, &SourceCaptioner::send_caption_text, Qt::QueuedConnection);
    QObject::connect(&timer, &QTimer::timeout, this, &SourceCaptioner::clear_output_timer_cb);
    timer.start(1000);

    info_log("SourceCaptioner, source '%s'", settings.caption_source_settings.caption_source_name.c_str());
    set_settings(settings);
}


void SourceCaptioner::clear_settings(bool send_signal) {
    {
        std::lock_guard<recursive_mutex> lock(settings_change_mutex);
        audio_capture_session = nullptr;
        caption_parser = nullptr;
        captioner = nullptr;
    }
    if (send_signal)
        not_not_captioning_status();
}

CaptionerSettings SourceCaptioner::get_settings() {
    std::lock_guard<recursive_mutex> lock(settings_change_mutex);
    return settings;
}

bool SourceCaptioner::set_settings(CaptionerSettings new_settings) {
    info_log("set_settingsset_settingsset_settings");
    clear_settings(false);
    {
        std::lock_guard<recursive_mutex> lock(settings_change_mutex);

        settings = new_settings;

        if (new_settings.caption_source_settings.caption_source_name.empty()) {
            warn_log("SourceCaptioner set_settings, empty source given.", new_settings.caption_source_settings.caption_source_name.c_str());
            clear_settings();
            return false;
        }

        OBSSource caption_source = obs_get_source_by_name(new_settings.caption_source_settings.caption_source_name.c_str());
        if (!caption_source) {
            warn_log("SourceCaptioner set_settings, no caption source with name: '%s'",
                     new_settings.caption_source_settings.caption_source_name.c_str());
            clear_settings();
            return false;
        }

        OBSSource mute_source;
        if (new_settings.caption_source_settings.mute_when == CAPTION_SOURCE_MUTE_TYPE_USE_OTHER_MUTE_SOURCE) {
            mute_source = obs_get_source_by_name(new_settings.caption_source_settings.mute_source_name.c_str());

            if (!mute_source) {
                warn_log("SourceCaptioner set_settings, no mute source with name: '%s'",
                         new_settings.caption_source_settings.mute_source_name.c_str());
                clear_settings();
                return false;
            }
        }

        auto caption_cb = std::bind(&SourceCaptioner::on_caption_text_callback, this, std::placeholders::_1, std::placeholders::_2);
        captioner = std::make_unique<ContinuousCaptions>(new_settings.stream_settings);
        captioner->on_caption_cb_handle.set(caption_cb, true);
        caption_parser = std::make_unique<CaptionResultHandler>(new_settings.format_settings);

        try {
            resample_info resample_to = {16000, AUDIO_FORMAT_16BIT, SPEAKERS_MONO};
            audio_chunk_data_cb audio_cb = std::bind(&SourceCaptioner::on_audio_data_callback, this,
                                                     std::placeholders::_1, std::placeholders::_2);

            auto audio_status_cb = std::bind(&SourceCaptioner::on_audio_capture_status_change_callback, this, std::placeholders::_1);

            audio_capture_session = std::make_unique<AudioCaptureSession>(caption_source, mute_source, audio_cb, audio_status_cb,
                                                                          resample_to,
//                                                                      MUTED_SOURCE_DISCARD_WHEN_MUTED);
                                                                          MUTED_SOURCE_REPLACE_WITH_ZERO);
        }
        catch (std::string err) {
            warn_log("couldn't create AudioCaptureSession, %s", err.c_str());
            clear_settings();
            return false;
        }
        catch (...) {
            warn_log("couldn't create AudioCaptureSession");
            clear_settings();
            return false;
        }

        info_log("starting captioning source '%s'", new_settings.caption_source_settings.caption_source_name.c_str());
        return true;
    }
}

void SourceCaptioner::on_audio_capture_status_change_callback(const audio_source_capture_status status) {
    info_log("capture status change %d ", status);
    emit audio_capture_status_changed(status);
}

void SourceCaptioner::on_audio_data_callback(const uint8_t *data, const size_t size) {
//    info_log("audio data");
    if (captioner) {
        captioner->queue_audio_data((char *) data, size);
    }
    audio_chunk_count++;

}

void SourceCaptioner::clear_output_timer_cb() {
//    info_log("clear timer checkkkkkkkkkkkkkkk");

    {
        std::lock_guard<recursive_mutex> lock(settings_change_mutex);
        if (!this->settings.format_settings.caption_timeout_enabled || this->last_caption_cleared)
            return;

        double secs_since_last_caption = std::chrono::duration_cast<std::chrono::duration<double >>(
                std::chrono::steady_clock::now() - this->last_caption_at).count();

        if (secs_since_last_caption <= this->settings.format_settings.caption_timeout_seconds)
            return;

        info_log("last caption line was sent %f secs ago, > %f, clearing",
                 secs_since_last_caption, this->settings.format_settings.caption_timeout_seconds);

        this->last_caption_cleared = true;
    }

    int active_delay = 0;
    obs_output_t *output = obs_frontend_get_streaming_output();
    if (output) {
//        info_log("built caption lines, sending: '%s'", output_caption_line.c_str());
        active_delay = obs_output_get_active_delay(output);
        obs_output_output_caption_text2(output, "", 0.01);
        obs_output_release(output);
    }

    emit caption_result_received(nullptr, false, true, active_delay);
}

void SourceCaptioner::send_caption_text(const string text, int send_in_secs) {
    if (send_in_secs) {
        auto call = [this, text, send_in_secs]() {
            info_log("SLOT sending lines, was delayed, waited %d  '%s'", send_in_secs, text.c_str());
            obs_output_t *output = obs_frontend_get_streaming_output();
            if (output) {
                // TODO: add must_match_output_delay bool param, check if delay is still the same if true
                // to avoid old ones firing after delay was turned off?

                this->caption_was_output();
                obs_output_output_caption_text2(output, text.c_str(), 0.01);
                obs_output_release(output);
            }
        };
        this->timer.singleShot(send_in_secs * 1000, this, call);
    } else {
        info_log("SLOT sending lines direct, sending,  '%s'", text.c_str());
        obs_output_t *output = obs_frontend_get_streaming_output();
        if (output) {
            this->caption_was_output();
            obs_output_output_caption_text2(output, text.c_str(), 0.01);
            obs_output_release(output);
        }
    }
}

void SourceCaptioner::on_caption_text_callback(const string &caption_obj, bool interrupted) {
    shared_ptr<CaptionResult> result;
    string output_caption_line;
    {
        std::lock_guard<recursive_mutex> lock(settings_change_mutex);

//        info_log("got caption %s", caption_obj.c_str());
        if (caption_parser) {
            result = caption_parser->parse_caption_object(caption_obj, true);
            if (interrupted)
                caption_parser->clear_history();

//        info_log("hmm %p", result.get());
            if (!result)
                return;

            if (!result->output_lines.empty()) {
//                info_log("got lines %lu", result->output_lines.size());

                // "\n".join(lines) or " ".join() depending on setting
                for (string &a_line: result->output_lines) {
//                info_log("a line: %s", a_line.c_str());
                    if (!output_caption_line.empty())
                        if (settings.format_settings.caption_insert_newlines)
                            output_caption_line.push_back('\n');
                        else
                            output_caption_line.push_back(' ');

                    output_caption_line.append(a_line);
                }
            }
        }
    }

    int active_delay_sec = 0;
    if (!output_caption_line.empty()) {
        active_delay_sec = this->output_caption_text(output_caption_line);
    }

    if (result) {
        emit caption_result_received(result, interrupted, false, active_delay_sec);
    }
}

int SourceCaptioner::output_caption_text(const string &line) {
    int active_delay_sec = 0;

    obs_output_t *output = obs_frontend_get_streaming_output();
    if (output) {
        active_delay_sec = obs_output_get_active_delay(output);
        if (active_delay_sec) {
            debug_log("queueing caption lines, preparing delay: %d,  '%s'",
                      active_delay_sec, line.c_str());

            string text(line);
            emit this->caption_text_line_received(text, active_delay_sec);

        } else {
            debug_log("sending caption lines, sending direct now: '%s'", line.c_str());
            this->caption_was_output();
            obs_output_output_caption_text2(output, line.c_str(), 0.01);
        }
    } else {
//            info_log("built caption lines, no output, not sending, not live?: '%s'", output_caption_line.c_str());
    }
    obs_output_release(output);

    return active_delay_sec;
}

SourceCaptioner::~SourceCaptioner() {
    clear_settings(false);
}

void SourceCaptioner::not_not_captioning_status() {
    emit audio_capture_status_changed(-1);
}

void SourceCaptioner::caption_was_output() {
    this->last_caption_at = std::chrono::steady_clock::now();
    this->last_caption_cleared = false;
}
