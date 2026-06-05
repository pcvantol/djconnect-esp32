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
    {"boot_authorizing_spotify", "Authorizing Spotify...", "Spotify autoriseren..."},
    {"boot_ble_setup_failed", "BLE setup failed\nUse setup page", "BLE setup mislukt\nGebruik setup pagina"},
    {"boot_ble_setup_ok", "BLE setup OK...", "BLE setup OK..."},
    {"boot_booting", "Booting...", "Opstarten..."},
    {"boot_connecting_wifi", "Connecting to WiFi...", "Verbinden met WiFi..."},
    {"boot_connect_setup_wifi", "Connect to WiFi", "Verbind met WiFi"},
    {"boot_device_setup", "Device setup", "Device setup"},
    {"boot_factory_reset", "Factory reset...", "Fabrieksreset..."},
    {"boot_pair_timeout_sleeping", "Pair timeout\nSleeping...", "Koppeltimeout\nSlapen..."},
    {"boot_paired", "SpotifyDJ\nPaired", "SpotifyDJ\nGekoppeld"},
    {"boot_please_charge", "Please charge device", "Laad het device op"},
    {"boot_reset_pairing", "Reset pairing...", "Koppeling resetten..."},
    {"boot_setup_device", "Setup device", "Device instellen"},
    {"boot_setup_ok", "Setup OK...", "Setup OK..."},
    {"boot_setup_timeout_sleeping", "Setup timeout\nSleeping...", "Setup timeout\nSlapen..."},
    {"boot_syncing_clock", "Syncing clock...", "Klok synchroniseren..."},
    {"boot_testing_spotify", "Testing Spotify...", "Spotify testen..."},
    {"boot_testing_wifi", "Testing WiFi...", "WiFi testen..."},
    {"charging", "Charging...", "Laden..."},
    {"firmware_update_progress", "Firmware update\nin progress..", "Firmware update\nbezig.."},
    {"center_back", "Center press = back", "Middenknop = terug"},
    {"center_liked_proxy", "Center: start Liked Proxy", "Midden: start Liked Proxy"},
    {"connected", "Connected", "Verbonden"},
    {"current_song", "Current Song", "Huidig nummer"},
    {"deep_sleep_after", "Turn off after", "Uitzetten na"},
    {"dim_timeout", "Screen dim timeout", "Scherm uit na"},
    {"disconnected", "Disconnected", "Niet verbonden"},
    {"factory_reset", "Factory reset", "Fabrieksreset"},
    {"factory_reset_title", "Factory reset?", "Fabrieksreset?"},
    {"language", "Language", "Taal"},
    {"language_english", "English", "Engels"},
    {"language_dutch", "Dutch", "Nederlands"},
    {"logs", "Logs", "Logs"},
    {"menu", "Menu", "Menu"},
    {"mqtt", "MQTT", "MQTT"},
    {"no_active_device", "No active device", "Geen actief apparaat"},
    {"no_outputs", "No outputs loaded", "Geen outputs geladen"},
    {"no_playback", "No Playback", "Geen playback"},
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
    {"playing", "Playing", "Speelt"},
    {"queue_empty", "Queue empty", "Wachtrij leeg"},
    {"refresh_token_missing", "Refresh token missing", "Refresh token ontbreekt"},
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
    {"spotify_play_mode", "Spotify play mode", "Spotify speelmodus"},
    {"spotify_connected", "Spotify connected", "Spotify verbonden"},
    {"spotify_authorization_failed", "Spotify authorization failed", "Spotify autorisatie mislukt"},
    {"spotify_authorization_failed_sentence", "Spotify authorization failed.", "Spotify autorisatie mislukt."},
    {"spotify_client_and_refresh_required", "Spotify client ID and refresh token are required", "Spotify client ID en refresh token zijn verplicht"},
    {"spotify_not_connected", "Spotify not connected", "Spotify niet verbonden"},
    {"spotify_refresh_saved_ok", "Spotify refresh token saved and authorization OK", "Spotify refresh token opgeslagen en autorisatie OK"},
    {"spotify_select_output_first", "Select a Spotify output first", "Selecteer eerst een Spotify output"},
    {"voice_connecting", "Connecting...", "Verbinden..."},
    {"voice_ha_auth_failed", "HA authorization failed. Reset pairing and pair again.", "HA autorisatie mislukt. Reset de koppeling en koppel opnieuw."},
    {"voice_ha_endpoint_missing", "HA voice endpoint not found. Reset pairing and set up the SpotifyDJ integration again.", "HA voice endpoint niet gevonden. Reset de koppeling en stel de SpotifyDJ integratie opnieuw in."},
    {"voice_listening", "Listening...", "Luisteren..."},
    {"voice_processing", "Processing...", "Verwerken..."},
    {"theme", "Theme", "Thema"},
    {"theme_auto", "Auto", "Auto"},
    {"theme_dark", "Dark", "Donker"},
    {"theme_light", "Light", "Licht"},
    {"turn_off_device", "Turn off device", "Device uitschakelen"},
    {"up_next", "Up next", "Volgende nummer"},
    {"volume", "Vol", "Vol"},
    {"web", "Web", "Web"},
    {"wifi", "WiFi", "WiFi"},
    {"wifi_disconnected", "WiFi disconnected", "WiFi niet verbonden"},
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
