// Runtime translations for display and web-facing labels. Logs intentionally stay English.
#include "I18n.h"

namespace {
Language CurrentLanguage = Language::English;

struct Entry {
  const char *key;
  const char *en;
  const char *nl;
};

constexpr Entry Entries[] = {
    {"about", "About", "Over"},
    {"album_art_no_art", "No art", "Geen cover"},
    {"audio_feedback", "Audio feedback", "Audiofeedback"},
    {"back_top_button", "Back = top button press", "Terug = bovenknop"},
    {"battery", "Battery", "Batterij"},
    {"brightness", "Screen brightness", "Schermhelderheid"},
    {"boot_authorizing_spotify", "Connecting playback...", "Afspelen verbinden..."},
    {"boot_connecting_playback", "Connecting playback...", "Afspelen verbinden..."},
    {"boot_ble_setup_failed", "BLE setup failed\nUse setup page", "BLE setup mislukt\nGebruik setup pagina"},
    {"boot_ble_setup_ok", "BLE setup OK...", "BLE setup OK..."},
    {"boot_booting", "Booting...", "Opstarten..."},
    {"boot_checking_firmware", "Checking firmware...", "Firmware controleren..."},
    {"boot_connecting_wifi", "Connecting to WiFi...", "Verbinden met WiFi..."},
    {"boot_connect_setup_wifi", "Connect to WiFi", "Verbind met WiFi"},
    {"boot_device_setup", "Device setup", "Device setup"},
    {"boot_factory_reset", "Factory reset...", "Fabrieksreset..."},
    {"boot_pair_timeout_sleeping", "Pair timeout\nSleeping...", "Koppeltimeout\nSlapen..."},
    {"boot_paired", "SpotifyDJ paired", "SpotifyDJ gekoppeld"},
    {"boot_please_charge", "Please charge device", "Laad het device op"},
    {"boot_reset_pairing", "Reset pairing...", "Koppeling resetten..."},
    {"boot_setup_device", "Setup device", "Device instellen"},
    {"boot_setup_ok", "Setup OK...", "Setup OK..."},
    {"boot_setup_timeout_sleeping", "Setup timeout\nSleeping...", "Setup timeout\nSlapen..."},
    {"boot_syncing_clock", "Syncing clock...", "Klok synchroniseren..."},
    {"boot_testing_spotify", "Testing playback...", "Afspelen testen..."},
    {"boot_testing_wifi", "Testing WiFi...", "WiFi testen..."},
    {"charging", "Charging...", "Laden..."},
    {"checking_playback", "Checking playback", "Playback controleren"},
    {"firmware_update_progress", "Firmware update\nin progress..", "Firmware update\nbezig.."},
    {"ha_pairing_invalid", "Home Assistant pairing invalid. Reset pairing and pair again.", "Home Assistant koppeling ongeldig. Reset de koppeling en koppel opnieuw."},
    {"center_back", "Center press = back", "Terug = middenknop"},
    {"center_liked_proxy", "Start Liked Proxy = center", "Start Liked Proxy = middenknop"},
    {"connected", "Connected", "Verbonden"},
    {"current_song", "Current Song", "Huidig nummer"},
    {"deep_sleep_after", "Turn off after", "Uitzetten na"},
    {"device_not_found", "Device not found", "Device niet gevonden"},
    {"dim_timeout", "Screen dim timeout", "Scherm uit na"},
    {"disconnected", "Disconnected", "Niet verbonden"},
    {"factory_reset", "Factory reset", "Fabrieksreset"},
    {"factory_reset_title", "Factory reset?", "Fabrieksreset?"},
    {"language", "Language", "Taal"},
    {"language_english", "English", "Engels"},
    {"language_dutch", "Dutch", "Nederlands"},
    {"logs", "Logs", "Logs"},
    {"pong", "Pong", "Pong"},
    {"log_level", "Log level", "Logniveau"},
    {"log_level_debug", "Debug", "Debug"},
    {"log_level_info", "Info", "Info"},
    {"log_level_warning", "Warning", "Waarschuwing"},
    {"log_level_error", "Error", "Fout"},
    {"liked_proxy_started", "Liked Proxy started", "Liked Proxy gestart"},
    {"liked_proxy_not_found", "Liked Proxy playlist not found", "Liked Proxy afspeellijst niet gevonden"},
    {"loading_next_track", "Loading next track", "Volgende nummer laden"},
    {"loading_outputs", "Loading outputs", "Geluidsuitgangen laden"},
    {"loading_playlists", "Loading playlists", "Afspeellijsten laden"},
    {"loading_previous_track", "Loading previous track", "Vorige nummer laden"},
    {"loading_queue", "Loading queue", "Wachtrij laden"},
    {"menu", "Menu", "Menu"},
    {"next_track", "Next track", "Volgend nummer"},
    {"no_active_device", "No active device", "Geen actief apparaat"},
    {"no_current_song", "No current song", "Geen huidig nummer"},
    {"no_output_selected", "No output selected", "Geen geluidsuitgang geselecteerd"},
    {"no_outputs", "No outputs loaded", "Geen geluidsuitgangen geladen"},
    {"no_playback", "No Playback", "Geen playback"},
    {"no_playlists", "No playlists", "Geen afspeellijsten"},
    {"none", "None", "Geen"},
    {"not_paired", "Not paired", "Niet gekoppeld"},
    {"nothing_playing", "Nothing playing", "Niets speelt"},
    {"now_playing", "Now Playing", "Speelt nu"},
    {"off", "off", "uit"},
    {"on", "on", "aan"},
    {"outputs", "Sound outputs", "Geluidsuitgangen"},
    {"pairing_code", "Pairing code", "Koppelcode"},
    {"paused", "Paused", "Gepauzeerd"},
    {"playlists", "Playlists", "Afspeellijsten"},
    {"playlist_started", "Playlist started", "Afspeellijst gestart"},
    {"playing", "Playing", "Speelt"},
    {"playing_on", "Playing on", "Speelt op"},
    {"previous_track", "Previous track", "Vorig nummer"},
    {"queue_empty", "Queue empty", "Wachtrij leeg"},
    {"refreshing", "Refreshing", "Verversen"},
    {"playback_credentials_unavailable", "Playback credentials unavailable", "Afspeelgegevens niet beschikbaar"},
    {"reset_pairing", "Reset Home Assistant pairing", "Home Assistant koppeling resetten"},
    {"reset_pairing_title", "Reset Home Assistant pairing?", "Home Assistant koppeling resetten?"},
    {"restart_device", "Restart device", "Device herstarten"},
    {"restarting", "Restarting...", "Herstarten..."},
    {"turning_off", "Turning off...", "Uitzetten..."},
    {"setup_success_restart", "Setup successful. Restarting into normal mode...", "Setup gelukt. Herstarten naar normale modus..."},
    {"battery_ok_restart", "Battery OK\nRestarting...", "Batterij OK\nHerstarten..."},
    {"wifi_ok_restart", "WiFi OK. Restarting...", "WiFi OK. Herstarten..."},
    {"selected", "selected", "geselecteerd"},
    {"confirm_no", "No", "Nee"},
    {"confirm_no_go_back", "No, go back", "Nee, terug"},
    {"confirm_yes_reset_pairing", "Yes, reset pairing", "Ja, koppeling resetten"},
    {"confirm_yes_wipe_setup", "Yes, wipe setup", "Ja, setup wissen"},
    {"settings", "Settings", "Instellingen"},
    {"speaker_volume", "Speaker volume", "Speakervolume"},
    {"spotify_play_mode", "Play mode", "Speelmodus"},
    {"spotify_connected", "Playback connected", "Afspelen verbonden"},
    {"playback_connected", "Playback connected", "Afspelen verbonden"},
    {"spotify_authorization_failed", "Playback authorization failed", "Afspeelautorisatie mislukt"},
    {"spotify_authorization_failed_sentence", "Playback authorization failed.", "Afspeelautorisatie mislukt."},
    {"spotify_client_and_refresh_required", "Playback credentials are managed in Home Assistant", "Afspeelgegevens worden beheerd in Home Assistant"},
    {"spotify_not_connected", "Playback not connected", "Afspelen niet verbonden"},
    {"spotify_refresh_saved_ok", "Playback credentials saved", "Afspeelgegevens opgeslagen"},
    {"spotify_select_output_first", "Select an output first", "Selecteer eerst een output"},
    {"starting_liked_proxy", "Starting Liked Proxy", "Liked Proxy starten"},
    {"starting_output", "Starting", "Starten"},
    {"starting_playlist", "Starting playlist", "Afspeellijst starten"},
    {"stress_test", "Stress test", "Stresstest"},
    {"stress_test_started", "Stress test started", "Stresstest gestart"},
    {"stress_test_stopped", "Stress test stopped", "Stresstest gestopt"},
    {"testing_wifi", "Testing WiFi", "WiFi testen"},
    {"voice_connecting", "Connecting...", "Verbinden..."},
    {"voice_ha_auth_failed", "HA authorization failed. Reset pairing and pair again.", "HA autorisatie mislukt. Reset de koppeling en koppel opnieuw."},
    {"voice_ha_endpoint_missing", "HA voice endpoint not found. Reset pairing and set up the SpotifyDJ integration again.", "HA voice endpoint niet gevonden. Reset de koppeling en stel de SpotifyDJ integratie opnieuw in."},
    {"voice_listening", "Listening...", "Luisteren..."},
    {"voice_processing", "Processing...", "Verwerken..."},
    {"voice_response_audio_failed", "Voice response audio failed", "Voice response audio mislukt"},
    {"voice_response_played", "Voice response played", "Voice response afgespeeld"},
    {"web_settings_saved", "Web settings saved", "Webinstellingen opgeslagen"},
    {"theme", "Theme", "Thema"},
    {"theme_auto", "Auto", "Auto"},
    {"theme_dark", "Dark", "Donker"},
    {"theme_light", "Light", "Licht"},
    {"turn_off_device", "Turn off device", "Device uitschakelen"},
    {"up_next", "Up next", "Volgende nummer"},
    {"volume", "Vol", "Vol"},
    {"web", "Web", "Web"},
    {"wifi", "WiFi", "WiFi"},
    {"wifi_connected", "WiFi connected", "WiFi verbonden"},
    {"wifi_failed", "WiFi failed", "WiFi mislukt"},
    {"wifi_disconnected", "WiFi disconnected", "WiFi niet verbonden"},
    {"wifi_test_failed", "WiFi test failed", "WiFi test mislukt"},
    {"center_select", "Select = center", "Selecteren = middenknop"},
    {"retry_connect", "Retry connect", "Opnieuw verbinden"},
    {"setup_portal_active_10m", "Portal active for 10 minutes", "Portal actief voor 10 minuten"},
    {"setup_turn_off_hint", "Turn off = center", "Uitzetten = middenknop"},
};
}  // namespace

void I18n::setLanguage(Language language) {
  CurrentLanguage = language;
}

Language I18n::language() {
  return CurrentLanguage;
}

String I18n::languageCode() {
  return CurrentLanguage == Language::Dutch ? "nl" : "en";
}

Language I18n::languageFromCode(const String &code) {
  String normalized = code;
  normalized.toLowerCase();
  return normalized == "nl" || normalized == "dutch" || normalized == "nederlands"
             ? Language::Dutch
             : Language::English;
}

const char *I18n::text(const char *key) {
  for (const Entry &entry : Entries) {
    if (strcmp(entry.key, key) == 0) {
      return CurrentLanguage == Language::Dutch ? entry.nl : entry.en;
    }
  }
  return key;
}

String I18n::onOff(bool value) {
  return text(value ? "on" : "off");
}

String I18n::connected(bool value) {
  return text(value ? "connected" : "disconnected");
}
