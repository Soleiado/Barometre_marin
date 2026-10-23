# Barometre marin

Baromètre tactile pour **ESP32-S3** : trois cadrans (pression, température,
humidité), trois pages d'historique plein écran, interface web et sorties
**NMEA 2000** et **NMEA 0183**.

**Langue :** Français | [English](README.md)

Le projet tourne sur une carte **Waveshare ESP32-S3-Touch-LCD-4.3B** (dalle
couleurs 800 × 480, tactile capacitif GT911, horloge PCF85063A) associée à un
capteur **BME280** sur le bus I²C de la carte.

## Aperçu

| Écran de la carte | Interface web |
|:---:|:---:|
| <img src="docs/ecran_de_la_carte.jpg" alt="Baromètre en fonctionnement : cadrans humidité et température, historique de pression" width="400"> | <img src="docs/page_web.png" alt="Page web : valeurs courantes et historique de pression" width="400"> |

*La même mesure des deux côtés : les cadrans et l'historique de pression sur la
carte, les valeurs en direct et le graphique sur la page web.*

## Sommaire

- [Aperçu](#aperçu)
- [Fonctionnalités](#fonctionnalités)
- [Matériel](#matériel)
- [Compilation](#compilation)
- [Dépendances](#dépendances)
- [Configuration de TFT_eSPI](#configuration-de-tft_espi)
- [Utilisation de l'écran](#utilisation-de-lécran)
- [Interface web](#interface-web)
- [Sorties NMEA](#sorties-nmea)
- [Structure du projet](#structure-du-projet)
- [Notes techniques](#notes-techniques)
- [Licence](#licence)

## Fonctionnalités

### Écran

- Trois cadrans analogiques : pression (980 → 1030 hPa), température et humidité.
- Bandeau d'information : date, heure et pression instantanée.
- Trois pages d'historique plein écran (humidité, température, pression),
  ouvertes d'un appui sur le cadran correspondant, refermées d'un appui
  n'importe où. L'appui sur le graphique de pression ouvre l'historique de
  pression.
- Fenêtre d'historique commune aux trois séries : **6 h, 12 h, 24 h ou 96 h**.
  L'axe des temps couvre exactement la durée choisie, avec un point par
  intervalle de 15 min.
- Échelle de température fixe (−20 → 40 °C, ou −4 → 104 °F en degrés
  Fahrenheit), échelle de pression glissante sur 50 hPa — la même que celle
  affichée sur la page web.
- Mise en veille de l'écran après 5 min sans appui ; le premier appui rallume
  et revient à la page de mesure.
- Menu tactile : unité de température, fenêtre d'historique, configuration
  NMEA, date et heure, redémarrage de l'écran, sortie.

### Interface web

- Valeurs courantes en direct et graphique d'historique de la pression
  (6 / 12 / 24 / 96 h), avec la même échelle que l'écran.
- Interface **bilingue français / anglais**, choisie dans la page de réglages ;
  le choix est conservé en mémoire et ne concerne que le web (l'écran de la
  carte reste en français).
- Page de réglages : langue, redémarrage de l'écran (sans perte d'historique),
  redémarrage de la carte, unité de température, débit NMEA 0183, réseau WiFi,
  date et heure.
- Connexion au réseau domestique, avec point d'accès de secours automatique si
  le réseau est absent. Réglages conservés en NVS (mémoire flash) et rechargés
  au démarrage.

### NMEA

- **NMEA 2000** sur le bus CAN : PGN 130312 (température), 130313 (humidité) et
  130314 (pression), émis une fois par seconde.
- **NMEA 0183** sur le port RS485 : trame `$IIXDR` (humidité, température,
  pression) toutes les 10 s, débit configurable de 4800 à 38400 bauds, ou
  sortie désactivée.
- Les valeurs NMEA restent toujours en unités métriques (°C, %, bar), même
  lorsque l'écran est réglé en Fahrenheit.

## Matériel

| Élément | Détail |
|---|---|
| Carte | Waveshare ESP32-S3-Touch-LCD-4.3B (ESP32-S3, 16 Mo de flash, PSRAM OPI) |
| Dalle | RGB 800 × 480, pilotée par `ESP32_Display_Panel` |
| Tactile | GT911 (I²C), expandsion CH422G pour le rétro-éclairage et les entrées |
| Capteur | BME280 (pression, température, humidité) sur le bus I²C interne |
| Horloge | PCF85063A (0x51) sur la carte, ou DS3231 (0x68), sinon horloge logicielle |
| NMEA 0183 | RS485, GPIO 44 (TX) et 43 (RX) |
| NMEA 2000 | CAN via TWAI, GPIO 15 (TX) et 16 (RX), transceiver CAN externe |

## Compilation

### Récupérer le projet

```bash
git clone https://github.com/Soleiado/Barometre_marin.git
```

### Arduino IDE

1. Installer le cœur **esp32** (version 3.3.12 testée) via le gestionnaire de
   cartes, en ajoutant l'URL
   `https://espressif.github.io/arduino-esp32/package_esp32_index.json`.
2. Installer les bibliothèques listées ci-dessous.
3. Sélectionner la carte **ESP32S3 Dev Module** avec :
   - **Flash Size** : 16 MB
   - **PSRAM** : OPI PSRAM
   - **Partition Scheme** : 3 MB APP / 9 MB FAT
4. Ouvrir `Barometre_marin.ino` et téléverser.

Le fichier `build_opt.h` présent dans le projet est nécessaire : il définit
`CONFIG_SPIRAM_SUPPORT`, symbole encore testé par TFT_eSPI 2.5.43 pour allouer
les sprites en PSRAM (sans lui, le canvas de 800 × 480 ne peut pas être créé).

### arduino-cli

```bash
arduino-cli core install esp32:esp32@3.3.12

arduino-cli compile \
  --fqbn "esp32:esp32:esp32s3:FlashSize=16M,PSRAM=opi,PartitionScheme=app3M_fat9M_16MB" \
  --output-dir build .

arduino-cli upload -p COM8 \
  --fqbn "esp32:esp32:esp32s3:FlashSize=16M,PSRAM=opi,PartitionScheme=app3M_fat9M_16MB" \
  --input-dir build .
```

## Dépendances

Versions avec lesquelles le projet a été compilé et testé :

| Bibliothèque | Version | Rôle |
|---|---|---|
| `esp32:esp32` (Arduino-ESP32) | 3.3.12 | Cœur ESP32-S3 (IDF 5.4), WiFi, WebServer, Preferences, TWAI |
| [ESP32_Display_Panel](https://github.com/esp-arduino-libs/ESP32_Display_Panel) | 1.0.4 | Pilotage de la dalle RGB, du tactile GT911 et du CH422G |
| [esp-lib-utils](https://github.com/esp-arduino-libs/esp-lib-utils) | 0.3.0 | Requis par ESP32_Display_Panel |
| [ESP32_IO_Expander](https://github.com/esp-arduino-libs/ESP32_IO_Expander) | 1.1.1 | Requis par ESP32_Display_Panel (CH422G) |
| [TFT_eSPI](https://github.com/Bodmer/TFT_eSPI) | 2.5.43 | Canvas de dessin 800 × 480 en PSRAM |
| [TFT_eWidget](https://github.com/Bodmer/TFT_eWidget) | 0.0.5 | Boutons et champs de formulaire du menu tactile |
| [NMEA2000](https://github.com/ttlappalainen/NMEA2000) | 4.24.1 | Pile NMEA 2000 (pilote `NMEA2000_esp32_twai`) |

Les autres bibliothèques utilisées (`WiFi`, `WebServer`, `ESPmDNS`,
`Preferences`, `SPI`, `driver/i2c`) sont fournies avec le cœur ESP32.

Installation en une commande :

```bash
arduino-cli lib install \
  "ESP32_Display_Panel@1.0.4" \
  "esp-lib-utils@0.3.0" \
  "ESP32_IO_Expander@1.1.1" \
  "TFT_eSPI@2.5.43" \
  "TFT_eWidget@0.0.5" \
  "NMEA2000@4.24.1"
```

> Le dossier de bibliothèques du projet ne contient **aucune** modification
> locale : ce sont les versions publiées, telles quelles.

## Configuration de TFT_eSPI

TFT_eSPI ne pilote pas la dalle (c'est `ESP32_Display_Panel` qui s'en charge) :
il ne sert qu'à fournir la surface de dessin. Il doit toutefois être configuré,
sinon les polices et les couleurs ne correspondent pas.

Copier `extras/TFT_eSPI_User_Setup.h` par-dessus le fichier `User_Setup.h` du
dossier de la bibliothèque TFT_eSPI. Les lignes qui comptent :

```c
#define ST7796_DRIVER      // géométrie de référence, le canvas est créé en 800x480
#define TFT_BL   5
#define SPI_FREQUENCY  27000000
#define LOAD_GLCD
#define LOAD_FONT2         // petites étiquettes
#define LOAD_FONT4         // textes courants
#define LOAD_FONT6         // grands chiffres
#define LOAD_FONT7
#define LOAD_FONT8
#define LOAD_GFXFF
#define SMOOTH_FONT
```

## Utilisation de l'écran

| Zone tactile | Effet |
|---|---|
| Bandeau gris en bas à droite | ouvre / ferme le menu |
| Cadran d'humidité (en haut) | historique plein écran de l'humidité |
| Cadran de température (en bas) | historique plein écran de la température |
| Graphique de pression | historique plein écran de la pression |
| N'importe où sur un historique | retour à la page de mesure |

Le menu reste ouvert jusqu'à **Exit**. Ses entrées :

| Entrée | Contenu |
|---|---|
| Temp. Unit | Celsius / Fahrenheit (l'astérisque marque le choix courant) |
| Hist. Range | 6 h / 12 h / 24 h / 96 h |
| NMEA Config. | N2K seul, ou NMEA 0183 à 4800 / 9600 / 19200 / 38400 bauds |
| Date & Time | réglage de l'horloge (jour, mois, année, heure, minutes) |
| Restart Screen | réinitialise la dalle sans perdre l'historique (voir plus bas) |
| Exit | retour à la page de mesure |

## Interface web

Une fois le WiFi configuré, la page est accessible par
**http://barometre.local/** ou par l'adresse IP de la carte. Sans réseau connu,
la carte crée le point d'accès **Barometre** / **barometre123**
(**http://192.168.4.1/**), où la même page permet de saisir les identifiants du
réseau domestique.

| Page | Contenu |
|---|---|
| `/` | valeurs courantes, graphique d'historique, accès aux réglages |
| `/params` | langue, écran, unités, sortie NMEA, réseau WiFi, date et heure |
| `/wifi` | saisie du SSID et du mot de passe (redémarre la carte) |

### API JSON

| Route | Méthode | Paramètres | Réponse |
|---|---|---|---|
| `/api/data` | GET | — | pression, température, humidité, unité, mode d'historique, heure, état WiFi |
| `/api/history` | GET | `h=6\|12\|24\|96` | séries pression / température / humidité, échelles, horodatage |
| `/api/diag` | GET | — | diagnostic dalle et réseau (images, blocages, tas libre, PSRAM, RSSI) |
| `/api/panel` | GET | `display=1`, `restart=1`, `reboot=1`, `pclk=<Hz>` | actions sur la dalle |
| `/api/radio` | GET | `tx=<dBm>`, `sleep=0\|1` | puissance d'émission et économie d'énergie WiFi |
| `/time` | POST | `dt=AAAA-MM-JJTHH:MM` | réglage de l'horloge |
| `/save` | POST | `ssid`, `pass` | enregistrement du réseau et redémarrage |

Exemple :

```bash
curl "http://barometre.local/api/data"
{"ok":1,"p":1028.13,"t":19.2,"h":61,"fh":0,"mode":6,"time":"23/10/2026 08:41:03","sta":1,"ip":"192.168.0.22","rssi":-35}
```

### Réglages conservés

Enregistrés dans la NVS (namespace `baro`), rechargés au démarrage :

| Clé | Contenu |
|---|---|
| `ssid`, `pass` | réseau WiFi |
| `lang` | langue de l'interface web (0 = français, 1 = anglais) |
| `fah` | unité de température (Fahrenheit si vrai) |
| `nmea` | débit NMEA 0183 (0 = N2K seul) |
| `LastNodeAddress` | adresse de nœud NMEA 2000 |

## Sorties NMEA

| Sortie | Support | Contenu | Période |
|---|---|---|---|
| NMEA 2000 | CAN / TWAI, GPIO 15 (TX) et 16 (RX) | PGN 130312 (température), 130313 (humidité), 130314 (pression) | 1 s |
| NMEA 0183 | RS485, GPIO 44 (TX) et 43 (RX) | `$IIXDR` : humidité, température de l'air, pression barométrique | 10 s |

Exemple de trame NMEA 0183 :

```
$IIXDR,H,61.0,P,AirHumidity,C,19.2,C,AirTemp,P,1.0281,B,Barometer*4F
```

## Structure du projet

```
Barometre_marin.ino                 setup(), loop(), pages, menu, zones tactiles
screen.cpp / screen.h               dalle RGB, canvas TFT_eSPI en PSRAM, tactile, veille
meter.cpp / meter.h                 cadrans analogiques (aiguille, zones colorées)
barometer.cpp / barometer.h         série d'historique, graphiques, page de mesure
menu.cpp / menu.h                   boutons et formulaires tactiles
webui.cpp / webui.h                 serveur HTTP, pages HTML, API JSON
nmea.cpp / nmea.h                   sortie NMEA 0183 (RS485)
nmea2k.cpp / nmea2k.h               sortie NMEA 2000 (CAN / TWAI)
rtc_clock.cpp / rtc_clock.h         PCF85063A, DS3231 ou horloge logicielle
bme280_local.cpp / bme280_local.h   pilote BME280
i2c_bus.cpp / i2c_bus.h             bus I²C partagé (ancien pilote, imposé par ESP32_Display_Panel)
datetime_local.h                    type DateTime minimal (évite la dépendance à RTClib)
calibrate.cpp                       calibration du tactile
Definition.h                        constantes : délais, plages, numéros de page
esp_panel_board_supported_conf.h    sélection de la carte pour ESP32_Display_Panel
build_opt.h                         option de compilation (canvas en PSRAM)
extras/TFT_eSPI_User_Setup.h        configuration TFT_eSPI utilisée
docs/                               captures d'ecran reprises par ce README
README.md / README.fr.md            cette documentation, en anglais et en français
```

## Notes techniques

**Décalage vertical de l'image.** La dalle RGB peut se décaler d'une centaine de
pixels lorsque le bus mémoire est bloqué trop longtemps (écriture en flash,
rafale WiFi, copie du canvas). Les mesures prises pour l'éviter : PCLK à 16 MHz,
tampon de rebond (*bounce buffer*) de 20 lignes en RAM interne, invalidation du
cache du tampon, copie du canvas découpée en tranches de 8 lignes. Une
resynchronisation est en outre demandée automatiquement dès qu'un balayage
dépasse anormalement sa durée, et l'image peut être remise en place sans rien
perdre, depuis le menu tactile (**Restart Screen**) ou par
`/api/panel?display=1`.

**Historique en RAM.** Les séries d'historique tiennent en mémoire vive et sont
perdues à chaque redémarrage : aucune écriture périodique en flash, ce qui
éviterait justement d'interrompre le balayage de la dalle.

**Canvas en PSRAM.** L'interface est entièrement dessinée dans un sprite
TFT_eSPI de 800 × 480 pixels (768 Ko) placé en PSRAM, puis recopié dans le
tampon de la dalle.

**WiFi et dalle.** L'émission WiFi perturbe la dalle : la puissance est réduite
à 8,5 dBm et le mode d'économie `WIFI_PS_MAX_MODEM` est utilisé. La latence de
la page web en est légèrement augmentée (environ 0,4 s), ce qui reste
confortable.

**Pas d'accents dans les sources.** Les polices embarquées et la mise en page
sont prévues pour l'ASCII ; les textes de l'interface évitent donc les
caractères accentués.

**Interface web bilingue.** Les pages ne sont écrites qu'une fois : les textes
sont remplacés au moment de l'envoi (page principale) ou lus dans une table
français/anglais pour les pages construites dynamiquement. Aucune page n'est
dupliquée dans le binaire.

## Licence

Distribué sous licence **MIT**. Voir le fichier [LICENSE](LICENSE).
