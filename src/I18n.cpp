// Runtime translations for display and web-facing labels. Logs intentionally stay English.
#include "I18n.h"

namespace {
Language CurrentLanguage = Language::English;

struct Entry {
  const char *key;
  const char *values[I18n::SupportedLanguageCount];
};

constexpr Language SupportedLanguages[I18n::SupportedLanguageCount] = {
    Language::English,
    Language::Dutch,
    Language::German,
    Language::French,
    Language::Spanish,
};

constexpr Entry Entries[] = {
    {"about", {"About", "Over", "Info", "A propos", "Acerca de"}},
    {"album_art_no_art", {"No art", "Geen cover", "Kein Cover", "Sans pochette", "Sin portada"}},
    {"audio_feedback", {"Audio feedback", "Audiofeedback", "Audiofeedback", "Retour audio", "Audio"}},
    {"back_top_button", {"Back = top button", "Terug = bovenknop", "Zurueck = obere Taste", "Retour = bouton haut", "Atras = boton sup."}},
    {"battery", {"Battery", "Batterij", "Akku", "Batterie", "Bateria"}},
    {"brightness", {"Screen brightness", "Schermhelderheid", "Helligkeit", "Luminosite", "Brillo pantalla"}},
    {"change_wifi", {"Change WiFi", "WiFi wijzigen", "WiFi aendern", "Changer WiFi", "Cambiar WiFi"}},
    {"change_wifi_title", {"Change WiFi?", "WiFi wijzigen?", "WiFi aendern?", "Changer WiFi?", "Cambiar WiFi?"}},
    {"boot_authorizing_spotify", {"Connecting playback...", "Afspelen verbinden...", "Wiedergabe verbinden...", "Connexion lecture...", "Conectando reproduccion..."}},
    {"boot_connecting_playback", {"Connecting playback...", "Afspelen verbinden...", "Wiedergabe verbinden...", "Connexion lecture...", "Conectando reproduccion..."}},
    {"boot_ble_setup_failed", {"BLE setup failed\nUse setup page", "BLE setup mislukt\nGebruik setup pagina", "BLE-Setup fehlgeschlagen\nSetup-Seite nutzen", "BLE echoue\nPage setup", "BLE fallo\nUsa setup"}},
    {"boot_ble_setup_ok", {"BLE setup OK...", "BLE setup OK...", "BLE-Setup OK...", "BLE setup OK...", "BLE setup OK..."}},
    {"boot_booting", {"Booting...", "Opstarten...", "Startet...", "Demarrage...", "Iniciando..."}},
    {"boot_checking_firmware", {"Checking firmware...", "Firmware controleren...", "Firmware pruefen...", "Controle firmware...", "Comprobando firmware..."}},
    {"boot_connecting_wifi", {"Connecting to WiFi...", "Verbinden met WiFi...", "Mit WiFi verbinden...", "Connexion WiFi...", "Conectando WiFi..."}},
    {"boot_connect_setup_wifi", {"Connect to WiFi", "Verbind met WiFi", "Mit WiFi verbinden", "Connecter au WiFi", "Conectar WiFi"}},
    {"boot_device_setup", {"Device setup", "Device setup", "Geraet einrichten", "Config appareil", "Configurar dispositivo"}},
    {"boot_factory_reset", {"Factory reset...", "Fabrieksreset...", "Werksreset...", "Reinit usine...", "Restableciendo..."}},
    {"boot_pair_timeout_sleeping", {"Pair timeout\nSleeping...", "Koppeltimeout\nSlapen...", "Koppel-Timeout\nSchlafen...", "Association expiree\nVeille...", "Vinculo expiro\nDurmiendo..."}},
    {"boot_paired", {"DJConnect paired", "DJConnect gekoppeld", "DJConnect gekoppelt", "DJConnect associe", "DJConnect vinculado"}},
    {"boot_please_charge", {"Please charge device", "Laad het device op", "Bitte Geraet laden", "Chargez appareil", "Cargue dispositivo"}},
    {"boot_reset_pairing", {"Reset pairing...", "Koppeling resetten...", "Kopplung resetten...", "Reinit association...", "Restablecer vinculo..."}},
    {"boot_setup_device", {"Setup device", "Device instellen", "Geraet einrichten", "Configurer appareil", "Configurar dispositivo"}},
    {"boot_setup_ok", {"Setup OK...", "Setup OK...", "Setup OK...", "Setup OK...", "Setup OK..."}},
    {"boot_setup_timeout_sleeping", {"Setup timeout\nSleeping...", "Setup timeout\nSlapen...", "Setup-Timeout\nSchlafen...", "Setup expire\nVeille...", "Setup expiro\nDurmiendo..."}},
    {"boot_syncing_clock", {"Syncing clock...", "Klok synchroniseren...", "Uhr synchronisieren...", "Synchro horloge...", "Sincronizando reloj..."}},
    {"boot_testing_spotify", {"Testing playback...", "Afspelen testen...", "Wiedergabe testen...", "Test lecture...", "Probando reproduccion..."}},
    {"boot_testing_wifi", {"Testing WiFi...", "WiFi testen...", "WiFi testen", "Test WiFi", "Probando WiFi"}},
    {"charging", {"Charging...", "Laden...", "Laedt...", "Charge...", "Cargando..."}},
    {"checking_playback", {"Checking playback", "Playback controleren", "Wiedergabe pruefen", "Controle lecture", "Comprobando reproduccion"}},
    {"firmware_update_progress", {"Firmware update\nin progress..", "Firmware update\nbezig..", "Firmware-Update\nlaeuft..", "Mise a jour\nfirmware..", "Actualizando\nfirmware.."}},
    {"flyer", {"Sky Dash", "Sky Dash", "Sky Dash", "Sky Dash", "Sky Dash"}},
    {"games", {"Games", "Games", "Spiele", "Jeux", "Juegos"}},
    {"ha_pairing_invalid", {"Home Assistant pairing invalid. Reset pairing and pair again.", "Home Assistant koppeling ongeldig. Reset de koppeling en koppel opnieuw.", "HA-Kopplung ungueltig. Neu koppeln.", "Association HA invalide. Reassociez.", "Vinculo HA invalido. Vincula otra vez."}},
    {"help", {"Help", "Help", "Hilfe", "Aide", "Ayuda"}},
    {"help_top_short", {"Top short: back / next", "Boven kort: terug / volgende", "Oben kurz: zurueck/weiter", "Haut court: retour/suiv.", "Arriba corto: atras/sig."}},
    {"help_top_double", {"Top double: previous", "Boven dubbel: vorige", "Oben doppelt: vorheriger", "Haut double: precedent", "Arriba doble: anterior"}},
    {"help_top_hold", {"Top hold: menu", "Boven vasthouden: menu", "Oben halten: Menue", "Haut long: menu", "Arriba largo: menu"}},
    {"help_top_10s", {"Top 10s: restart", "Boven 10s: herstart", "Oben 10s: Neustart", "Haut 10s: reboot", "Arriba 10s: reinicio"}},
    {"help_center_short", {"Center short: play/pause", "Midden kort: play/pauze", "Mitte kurz: Play/Pause", "Centre court: play/pause", "Centro corto: play/pausa"}},
    {"help_center_hold", {"Center hold: voice PTT", "Midden vasthouden: voice PTT", "Mitte halten: Voice PTT", "Centre long: voix PTT", "Centro largo: voz PTT"}},
    {"help_encoder_turn", {"Encoder: volume / scroll", "Encoder: volume / scroll", "Encoder: Lautst./Scroll", "Encodeur: vol/scroll", "Encoder: vol/scroll"}},
    {"help_games", {"Games: center fires", "Games: midden schiet", "Spiele: Mitte feuert", "Jeux: centre tire", "Juegos: centro dispara"}},
    {"center_back", {"Center press = back", "Terug = middenknop", "Mitte = zurueck", "Centre = retour", "Centro = atras"}},
    {"center_liked_proxy", {"Default playlist = center", "Standaard playlist = middenknop", "Standardliste = Mitte", "Playlist defaut = centre", "Playlist predet. = centro"}},
    {"connected", {"Connected", "Verbonden", "Verbunden", "Connecte", "Conectado"}},
    {"current_song", {"Current song", "Huidig nummer", "Aktueller Song", "Titre actuel", "Cancion actual"}},
    {"deep_sleep_after", {"Turn off after", "Uitzetten na", "Ausschalten nach", "Eteindre apres", "Apagar despues"}},
    {"device_not_found", {"Device not found", "Device niet gevonden", "Geraet nicht gefunden", "Appareil introuvable", "Dispositivo no encontrado"}},
    {"dim_timeout", {"Screen dim timeout", "Scherm uit na", "Display dimmen nach", "Veille ecran apres", "Atenuar pantalla tras"}},
    {"disconnected", {"Disconnected", "Niet verbonden", "Getrennt", "Deconnecte", "Desconectado"}},
    {"factory_reset", {"Factory reset", "Fabrieksreset", "Werksreset", "Reinit usine", "Restablecer fabrica"}},
    {"factory_reset_title", {"Factory reset?", "Fabrieksreset?", "Werksreset?", "Reinit usine?", "Restablecer?"}},
    {"language", {"Language", "Taal", "Sprache", "Langue", "Idioma"}},
    {"language_english", {"English", "Engels", "Englisch", "Anglais", "Ingles"}},
    {"language_dutch", {"Dutch", "Nederlands", "Niederlaendisch", "Neerlandais", "Neerlandes"}},
    {"language_german", {"German", "Duits", "Deutsch", "Allemand", "Aleman"}},
    {"language_french", {"French", "Frans", "Franzoesisch", "Francais", "Frances"}},
    {"language_spanish", {"Spanish", "Spaans", "Spanisch", "Espagnol", "Espanol"}},
    {"logs", {"Logs", "Logs", "Logs", "Logs", "Logs"}},
    {"pong", {"Paddle Rally", "Paddle Rally", "Paddle Rally", "Paddle Rally", "Paddle Rally"}},
    {"asteroids", {"Meteor Run", "Meteor Run", "Meteor Run", "Meteor Run", "Meteor Run"}},
    {"maze_chase", {"Maze Chase", "Maze Chase", "Maze Chase", "Maze Chase", "Maze Chase"}},
    {"log_level", {"Log level", "Logniveau", "Logstufe", "Niveau log", "Nivel log"}},
    {"log_level_debug", {"Debug", "Debug", "Debug", "Debug", "Debug"}},
    {"log_level_info", {"Info", "Info", "Info", "Infos", "Info"}},
    {"log_level_warning", {"Warning", "Waarschuwing", "Warnung", "Avertissement", "Aviso"}},
    {"log_level_error", {"Error", "Fout", "Fehler", "Erreur", "Error"}},
    {"liked_proxy_started", {"Default playlist started", "Standaard playlist gestart", "Standardliste gestartet", "Playlist defaut lancee", "Playlist predet. iniciada"}},
    {"liked_proxy_not_found", {"Default playlist not found", "Standaard playlist niet gevonden", "Standardliste nicht gefunden", "Playlist defaut introuvable", "Playlist predet. no encontrada"}},
    {"loading_next_track", {"Loading next track", "Volgende nummer laden", "Naechsten Titel laden", "Chargement suivant", "Cargando siguiente"}},
    {"loading_outputs", {"Loading outputs", "Geluidsuitgangen laden", "Ausgaenge laden", "Chargement sorties", "Cargando salidas"}},
    {"loading_playlists", {"Loading playlists", "Afspeellijsten laden", "Playlists laden", "Chargement playlists", "Cargando playlists"}},
    {"loading_previous_track", {"Loading previous track", "Vorige nummer laden", "Vorigen Titel laden", "Chargement precedent", "Cargando anterior"}},
    {"loading_queue", {"Loading queue", "Wachtrij laden", "Warteschlange laden", "Chargement file", "Cargando cola"}},
    {"menu", {"Menu", "Menu", "Menue", "Menu", "Menu"}},
    {"music", {"Music", "Muziek", "Musik", "Musique", "Musica"}},
    {"next_track", {"Next track", "Volgend nummer", "Naechster Titel", "Titre suivant", "Siguiente cancion"}},
    {"no_active_device", {"No active device", "Geen actief apparaat", "Kein aktives Geraet", "Aucun appareil actif", "Sin dispositivo activo"}},
    {"no_current_song", {"No current song", "Geen huidig nummer", "Kein aktueller Song", "Aucun titre actuel", "Sin cancion actual"}},
    {"no_output_selected", {"No output selected", "Geen geluidsuitgang geselecteerd", "Kein Ausgang gewaehlt", "Aucune sortie choisie", "Sin salida seleccionada"}},
    {"no_outputs", {"No outputs loaded", "Geen geluidsuitgangen geladen", "Keine Ausgaenge geladen", "Aucune sortie chargee", "Sin salidas cargadas"}},
    {"no_playback", {"No Playback", "Geen playback", "Keine Wiedergabe", "Pas de lecture", "Sin reproduccion"}},
    {"no_playlists", {"No playlists", "Geen afspeellijsten", "Keine Playlists", "Aucune playlist", "Sin playlists"}},
    {"none", {"None", "Geen", "Keine", "Aucun", "Ninguno"}},
    {"not_paired", {"Not paired", "Niet gekoppeld", "Nicht gekoppelt", "Non associe", "No vinculado"}},
    {"nothing_playing", {"Nothing playing", "Niets speelt", "Keine Wiedergabe", "Rien en lecture", "Nada sonando"}},
    {"now_playing", {"Now Playing", "Speelt nu", "Laeuft gerade", "Lecture", "Sonando"}},
    {"off", {"off", "uit", "aus", "desactive", "apagado"}},
    {"on", {"on", "aan", "ein", "active", "encendido"}},
    {"outputs", {"Sound outputs", "Geluidsuitgangen", "Audioausgaenge", "Sorties audio", "Salidas de audio"}},
    {"pairing_code", {"Pairing code", "Koppelcode", "Koppelcode", "Code association", "Codigo vinculo"}},
    {"pairing_ha_url", {"HA URL", "HA URL", "HA URL", "HA URL", "HA URL"}},
    {"paused", {"Paused", "Gepauzeerd", "Pausiert", "Pause", "Pausado"}},
    {"playlists", {"Playlists", "Afspeellijsten", "Playlists", "Playlists", "Playlists"}},
    {"playlist_started", {"Playlist started", "Afspeellijst gestart", "Playlist gestartet", "Playlist lancee", "Playlist iniciada"}},
    {"playback_stopped", {"Playback stopped", "Playback gestopt", "Wiedergabe gestoppt", "Lecture arretee", "Reproduccion detenida"}},
    {"playing", {"Playing", "Speelt", "Spielt", "Lecture", "Reproduciendo"}},
    {"playing_on", {"Playing on", "Speelt op", "Spielt auf", "Lecture sur", "Sonando en"}},
    {"previous_track", {"Previous track", "Vorig nummer", "Voriger Titel", "Titre precedent", "Cancion anterior"}},
    {"queue_empty", {"Queue empty", "Wachtrij leeg", "Warteschlange leer", "File vide", "Cola vacia"}},
    {"refreshing", {"Refreshing", "Verversen", "Aktualisieren", "Actualisation", "Actualizando"}},
    {"refresh_requested", {"Refresh requested", "Verversen aangevraagd", "Aktualisierung angefragt", "Actualisation demandee", "Actualizacion solicitada"}},
    {"playback_credentials_unavailable", {"Playback credentials unavailable", "Afspeelgegevens niet beschikbaar", "Wiedergabedaten fehlen", "Donnees lecture absentes", "Datos reproduccion ausentes"}},
    {"recent_output", {"Recent", "Recent", "Kuerzlich", "Recent", "Reciente"}},
    {"selected_song_started", {"Selected song started", "Gekozen nummer gestart", "Gewaehlter Titel gestartet", "Titre choisi lance", "Cancion elegida iniciada"}},
    {"reset_pairing", {"Reset Home Assistant pairing", "Home Assistant koppeling resetten", "Home Assistant Kopplung resetten", "Reinit association HA", "Restablecer vinculo HA"}},
    {"reset_pairing_title", {"Reset Home Assistant pairing?", "Home Assistant koppeling resetten?", "HA-Kopplung resetten?", "Reinit association HA?", "Restablecer vinculo HA?"}},
    {"restart_device", {"Restart device", "Device herstarten", "Geraet neu starten", "Redemarrer appareil", "Reiniciar dispositivo"}},
    {"restarting", {"Restarting...", "Herstarten...", "Neustart...", "Redemarrage...", "Reiniciando..."}},
    {"turning_off", {"Turning off...", "Uitzetten...", "Ausschalten...", "Extinction...", "Apagando..."}},
    {"setup_success_restart", {"Setup successful. Restarting into normal mode...", "Setup gelukt. Herstarten naar normale modus...", "Setup OK. Neustart...", "Setup OK. Redemarrage...", "Setup OK. Reiniciando..."}},
    {"reset_pairing_restart", {"Reset pairing requested. Restarting to pairing screen...", "Koppeling resetten aangevraagd. Herstarten naar koppelscherm...", "Kopplung resetten. Neustart zum Koppeln...", "Reinit association. Redemarrage...", "Vinculo restablecido. Reiniciando..."}},
    {"factory_reset_restart", {"Factory reset requested. Restarting into setup mode...", "Fabrieksreset aangevraagd. Herstarten naar setup...", "Werksreset angefragt. Neustart ins Setup...", "Reinit usine. Redemarrage setup...", "Restablecer fabrica. Reiniciando setup..."}},
    {"firmware_uploaded_restart", {"Firmware uploaded. Restarting...", "Firmware geupload. Herstarten...", "Firmware hochgeladen. Neustart...", "Firmware envoye. Redemarrage...", "Firmware subido. Reiniciando..."}},
    {"battery_ok_restart", {"Battery OK\nRestarting...", "Batterij OK\nHerstarten...", "Akku OK\nNeustart...", "Batterie OK\nRedemarrage...", "Bateria OK\nReiniciando..."}},
    {"wifi_ok_restart", {"WiFi OK. Restarting...", "WiFi OK. Herstarten...", "WiFi OK. Neustart...", "WiFi OK. Redemarrage...", "WiFi OK. Reiniciando..."}},
    {"cached_output_retry_hint", {"Open Spotify on that device and try again.", "Open Spotify op dat apparaat en probeer opnieuw.", "Oeffne Spotify auf diesem Geraet und versuche es erneut.", "Ouvrez Spotify sur cet appareil puis reessayez.", "Abre Spotify en ese dispositivo e intentalo de nuevo."}},
    {"selected", {"selected", "geselecteerd", "gewaehlt", "selectionne", "seleccionado"}},
    {"confirm_no", {"No", "Nee", "Nein", "Non", "No"}},
    {"confirm_no_go_back", {"No, go back", "Nee, terug", "Nein, zurueck", "Non, retour", "No, volver"}},
    {"confirm_yes_reset_pairing", {"Yes, reset pairing", "Ja, koppeling resetten", "Ja, Kopplung resetten", "Oui, reinit association", "Si, restablecer vinculo"}},
    {"confirm_yes_change_wifi", {"Yes, open WiFi portal", "Ja, WiFi portal openen", "Ja, WiFi-Portal", "Oui, portail WiFi", "Si, portal WiFi"}},
    {"confirm_yes_wipe_setup", {"Yes, wipe", "Ja, wissen", "Ja, loeschen", "Oui, effacer", "Si, borrar"}},
    {"settings", {"Settings", "Instellingen", "Einstellungen", "Reglages", "Ajustes"}},
    {"speaker_volume", {"Speaker volume", "Speakervolume", "Lautsprecher", "Volume HP", "Volumen altavoz"}},
    {"shuffle", {"Shuffle", "Shuffle", "Shuffle", "Shuffle", "Shuffle"}},
    {"shuffle_off", {"Shuffle off", "Shuffle uit", "Shuffle aus", "Shuffle desactive", "Shuffle apagado"}},
    {"shuffle_on", {"Shuffle on", "Shuffle aan", "Shuffle ein", "Shuffle active", "Shuffle encendido"}},
    {"repeat", {"Repeat", "Herhalen", "Wiederholen", "Repeter", "Repetir"}},
    {"repeat_off", {"Repeat off", "Herhalen uit", "Wiederholen aus", "Repeter desactive", "Repetir apagado"}},
    {"repeat_once", {"Repeat once", "Eenmaal herhalen", "Einmal wiederholen", "Repeter une fois", "Repetir una vez"}},
    {"repeat_infinite", {"Repeat infinite", "Oneindig herhalen", "Endlos wiederholen", "Repeter en boucle", "Repetir en bucle"}},
    {"spotify_connected", {"Playback connected", "Afspelen verbonden", "Wiedergabe verbunden", "Lecture connectee", "Reproduccion conectada"}},
    {"playback_connected", {"Playback connected", "Afspelen verbonden", "Wiedergabe verbunden", "Lecture connectee", "Reproduccion conectada"}},
    {"spotify_authorization_failed", {"Playback authorization failed", "Afspeelautorisatie mislukt", "Wiedergabe-Autorisierung fehlgeschlagen", "Autorisation lecture echouee", "Autorizacion reproduccion fallo"}},
    {"spotify_authorization_failed_sentence", {"Playback authorization failed.", "Afspeelautorisatie mislukt.", "Wiedergabe-Autorisierung fehlgeschlagen.", "Autorisation lecture echouee.", "Autorizacion reproduccion fallo."}},
    {"spotify_client_and_refresh_required", {"Playback credentials are managed in Home Assistant", "Afspeelgegevens worden beheerd in Home Assistant", "Wiedergabedaten in Home Assistant", "Identifiants lecture dans Home Assistant", "Credenciales en Home Assistant"}},
    {"spotify_not_connected", {"Playback not connected", "Afspelen niet verbonden", "Wiedergabe nicht verbunden", "Lecture non connectee", "Reproduccion desconectada"}},
    {"playback_backend_unavailable_hint", {"Check the Spotify connection in Home Assistant.", "Controleer de Spotify-koppeling in Home Assistant.", "Spotify in Home Assistant pruefen.", "Verifiez Spotify dans Home Assistant.", "Comprueba Spotify en Home Assistant."}},
    {"spotify_refresh_saved_ok", {"Playback credentials saved", "Afspeelgegevens opgeslagen", "Wiedergabedaten gespeichert", "Donnees lecture sauvees", "Datos reproduccion guardados"}},
    {"spotify_select_output_first", {"Select an output first", "Selecteer eerst een output", "Erst Ausgang waehlen", "Choisir sortie d'abord", "Elige salida primero"}},
    {"output_switched", {"Output switched", "Output gewisseld", "Ausgang gewechselt", "Sortie changee", "Salida cambiada"}},
    {"starting_liked_proxy", {"Starting default playlist", "Standaard playlist starten", "Standardliste starten", "Demarrage playlist defaut", "Iniciando playlist predet."}},
    {"starting_output", {"Starting", "Starten", "Starten", "Demarrage", "Iniciando"}},
    {"starting_playlist", {"Starting playlist", "Afspeellijst starten", "Playlist starten", "Demarrage playlist", "Iniciando playlist"}},
    {"stress_test", {"Stress test", "Stresstest", "Stresstest", "Test charge", "Prueba de carga"}},
    {"stress_test_started", {"Stress test started", "Stresstest gestart", "Stresstest gestartet", "Test charge lance", "Prueba carga iniciada"}},
    {"stress_test_stopped", {"Stress test stopped", "Stresstest gestopt", "Stresstest gestoppt", "Test charge arrete", "Prueba carga detenida"}},
    {"testing_wifi", {"Testing WiFi", "WiFi testen", "Testing WiFi", "Test WiFi", "Probando WiFi"}},
    {"voice_connecting", {"Connecting...", "Verbinden...", "Verbinden...", "Connexion...", "Conectando..."}},
    {"voice_ha_auth_failed", {"HA authorization failed. Reset pairing and pair again.", "HA autorisatie mislukt. Reset de koppeling en koppel opnieuw.", "HA-Autorisierung fehlgeschlagen. Neu koppeln.", "Autorisation HA echouee. Reassociez.", "Autorizacion HA fallo. Vincula otra vez."}},
    {"voice_ha_endpoint_missing", {"HA voice endpoint not found. Reset pairing and set up the DJConnect integration again.", "HA voice endpoint niet gevonden. Reset de koppeling en stel de DJConnect integratie opnieuw in.", "HA-Voice-Endpunkt fehlt. Neu koppeln.", "Endpoint vocal HA absent. Reassociez.", "Endpoint voz HA falta. Vincula otra vez."}},
    {"voice_listening", {"Listening...", "Luisteren...", "Hoere zu...", "Ecoute...", "Escuchando..."}},
    {"voice_dj_response", {"DJ announcement", "DJ aankondiging", "DJ-Ansage", "Annonce DJ", "Anuncio DJ"}},
    {"voice_no_dj_response", {"No DJ announcement", "Geen DJ aankondiging", "Keine DJ-Ansage", "Pas annonce DJ", "Sin anuncio DJ"}},
    {"voice_processing", {"Processing...", "Verwerken...", "Verarbeite...", "Traitement...", "Procesando..."}},
    {"voice_response_audio_failed", {"Voice response audio failed", "Voice response audio mislukt", "Voice-Audio fehlgeschlagen", "Audio vocal echoue", "Audio de voz fallo"}},
    {"voice_response_played", {"Voice response played", "Voice response afgespeeld", "Voice-Audio abgespielt", "Audio vocal joue", "Audio de voz reproducido"}},
    {"web_settings_saved", {"Web settings saved", "Webinstellingen opgeslagen", "Web-Einstellungen gespeichert", "Reglages web sauves", "Ajustes web guardados"}},
    {"wake_word", {"Wake word", "Wakeword", "Wakeword", "Mot reveil", "Palabra clave"}},
    {"theme", {"Theme", "Thema", "Theme", "Theme", "Tema"}},
    {"theme_auto", {"Auto", "Auto", "Auto", "Auto", "Auto"}},
    {"theme_dark", {"Dark", "Donker", "Dunkel", "Sombre", "Oscuro"}},
    {"theme_light", {"Light", "Licht", "Hell", "Clair", "Claro"}},
    {"turn_off_device", {"Turn off device", "Device uitschakelen", "Geraet ausschalten", "Eteindre appareil", "Apagar dispositivo"}},
    {"up_next", {"Queue", "Wachtrij", "Warteschlange", "File", "Cola"}},
    {"volume", {"Vol", "Vol", "Vol", "Vol", "Vol"}},
    {"web", {"Web", "Web", "Web", "Web", "Web"}},
    {"wifi", {"WiFi", "WiFi", "WiFi", "WiFi", "WiFi"}},
    {"wifi_connected", {"WiFi connected", "WiFi verbonden", "WiFi verbunden", "WiFi connecte", "WiFi conectado"}},
    {"wifi_failed", {"WiFi failed", "WiFi mislukt", "WiFi fehlgeschlagen", "WiFi echoue", "WiFi fallido"}},
    {"wifi_disconnected", {"WiFi disconnected", "WiFi niet verbonden", "WiFi getrennt", "WiFi deconnecte", "WiFi desconectado"}},
    {"wifi_test_failed", {"WiFi test failed", "WiFi test mislukt", "WiFi-Test fehlgeschlagen", "Test WiFi echoue", "Prueba WiFi fallo"}},
    {"wifi_test_started_restart", {"WiFi test started. Device restarts if it connects.", "WiFi-test gestart. Device herstart bij succes.", "WiFi-Test gestartet. Neustart bei Erfolg.", "Test WiFi lance. Redemarrage si OK.", "Prueba WiFi iniciada. Reinicia si conecta."}},
    {"center_select", {"Select = center", "Selecteren = middenknop", "Auswahl = Mitte", "Selection = centre", "Seleccion = centro"}},
    {"retry_connect", {"Retry connect", "Opnieuw verbinden", "Erneut verbinden", "Reconnecter", "Reconectar"}},
    {"setup_portal_active_10m", {"Setup mode active for 10 minutes", "Setup modus actief voor 10 minuten", "Setup 10 Minuten aktiv", "Setup actif 10 minutes", "Setup activo 10 minutos"}},
    {"setup_turn_off_hint", {"Turn off = center", "Uitzetten = middenknop", "Ausschalten = Mitte", "Eteindre = centre", "Apagar = centro"}},
};

constexpr bool allTranslationsPresent() {
  for (const Entry &entry : Entries) {
    if (entry.key == nullptr || entry.key[0] == '\0') {
      return false;
    }
    for (size_t index = 0; index < I18n::SupportedLanguageCount; index++) {
      if (entry.values[index] == nullptr || entry.values[index][0] == '\0') {
        return false;
      }
    }
  }
  return true;
}

static_assert(allTranslationsPresent(), "Every I18n key must have all supported language values");

size_t languageIndex(Language language) {
  for (size_t index = 0; index < I18n::SupportedLanguageCount; index++) {
    if (SupportedLanguages[index] == language) {
      return index;
    }
  }
  return 0;
}

String normalizedLanguageCode(const String &code) {
  String normalized = code;
  normalized.trim();
  normalized.toLowerCase();
  const int separator = normalized.indexOf('-');
  if (separator > 0) {
    normalized = normalized.substring(0, separator);
  }
  return normalized;
}

}  // namespace

void I18n::setLanguage(Language language) {
  CurrentLanguage = language;
}

Language I18n::language() {
  return CurrentLanguage;
}

String I18n::languageCode() {
  return languageCode(CurrentLanguage);
}

Language I18n::languageFromCode(const String &code) {
  const String normalized = normalizedLanguageCode(code);
  if (normalized == "nl" || normalized == "dutch" || normalized == "nederlands") {
    return Language::Dutch;
  }
  if (normalized == "de" || normalized == "german" || normalized == "deutsch") {
    return Language::German;
  }
  if (normalized == "fr" || normalized == "french" || normalized == "francais") {
    return Language::French;
  }
  if (normalized == "es" || normalized == "spanish" || normalized == "espanol") {
    return Language::Spanish;
  }
  return Language::English;
}

bool I18n::isSupportedLanguageCode(const String &code) {
  const String normalized = normalizedLanguageCode(code);
  for (size_t index = 0; index < SupportedLanguageCount; index++) {
    if (normalized == Logic::supportedLanguageCodeAt(index)) {
      return true;
    }
  }
  return false;
}

const char *I18n::languageCode(Language language) {
  return Logic::supportedLanguageCodeAt(languageIndex(language));
}

const char *I18n::languageLabelKey(Language language) {
  switch (language) {
    case Language::Dutch:
      return "language_dutch";
    case Language::German:
      return "language_german";
    case Language::French:
      return "language_french";
    case Language::Spanish:
      return "language_spanish";
    case Language::English:
    default:
      return "language_english";
  }
}

Language I18n::languageAt(size_t index) {
  return SupportedLanguages[index < SupportedLanguageCount ? index : 0];
}

const char *I18n::text(const char *key) {
  for (const Entry &entry : Entries) {
    if (strcmp(entry.key, key) == 0) {
      return entry.values[languageIndex(CurrentLanguage)];
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
