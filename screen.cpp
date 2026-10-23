
#include "screen.h"

#include <esp_display_panel.hpp>

#include "i2c_bus.h"       // acces direct au CH422G (registre WR_IO)

using namespace esp_panel::drivers;
using namespace esp_panel::board;

//---------------------------------------------------------------------
// Parent "factice" du canvas.
//
// TFT_eSprite exige un TFT_eSPI parent, mais TFT_eSPI n'est jamais
// initialise ici : l'objet sert uniquement au constructeur du sprite
// (il consulte son indicateur DMA, faux par defaut, ce qui force
// l'allocation du canvas en PSRAM).
//---------------------------------------------------------------------

static TFT_eSPI  s_parentTft;

static Board    *s_board  = nullptr;
static uint16_t *s_frame  = nullptr;
static Touch    *s_touch  = nullptr;
static bool      s_bounce = false;      // bounce buffer de la dalle active
static bool      s_ready  = false;      // dalle initialisee
static LCD      *s_lcd    = nullptr;    // pour resynchroniser la dalle
static int       s_restartErr = 0;      // dernier code d'erreur de resynchronisation
static int       s_pclk   = 0;          // frequence pixel demandee (Hz)
static Screen   *s_self   = nullptr;    // instance unique (pour les methodes statiques)

//---------------------------------------------------------------------
// Diagnostic : rythme de balayage de la dalle.
//
// Le controleur envoie une image complete toutes les ~45 ms. Un ecart
// nettement plus long signifie que le bus memoire a ete bloque au point que
// la dalle a manque de donnees : c'est exactement ce qui decale l'image.
// On compte ces incidents (/api/diag) pour savoir quand ils arrivent.
//---------------------------------------------------------------------

#define FRAME_STALL_US 52000            // ~26 ms attendus, 52 ms = incident

static volatile uint32_t s_frames = 0;  // images envoyees
static volatile uint32_t s_stalls = 0;  // images anormalement longues
static volatile uint32_t s_maxUs  = 0;  // plus long ecart observe (us)
static volatile int64_t  s_lastUs = 0;  // horodatage de la derniere image

static bool IRAM_ATTR onFrameComplete(esp_lcd_panel_handle_t panel,
                                      const esp_lcd_rgb_panel_event_data_t *edata,
                                      void *userCtx)
{
  (void)panel;
  (void)edata;
  (void)userCtx;

  int64_t now = esp_timer_get_time();

  if (s_lastUs) {
    uint32_t dt = (uint32_t)(now - s_lastUs);

    if (dt > s_maxUs)        s_maxUs = dt;
    if (dt > FRAME_STALL_US) s_stalls++;
  }

  s_lastUs = now;
  s_frames++;

  return false;
}

// Enregistre la surveillance du balayage.
//
// Attention : l'appel remplace l'ensemble des callbacks du pilote, y compris
// celui que la bibliotheque pose pour signaler la fin d'un rafraichissement.
// Le projet n'utilise pas drawBitmapWaitUntilFinish(), la consequence est
// donc nulle ici.
static void registerFrameCallback()
{
  if (!s_lcd) return;

  esp_lcd_rgb_panel_event_callbacks_t cbs = {};

  cbs.on_frame_buf_complete = onFrameComplete;

  esp_err_t err = esp_lcd_rgb_panel_register_event_callbacks(
                    s_lcd->getRefreshPanelHandle(), &cbs, nullptr);

  ESP_LOGI("screen", "surveillance du balayage : %s", err == ESP_OK ? "ok" : "refusee");
}

//---------------------------------------------------------------------

Screen::Screen() : TFT_eSprite(&s_parentTft)
{
  s_self = this;
}

//---------------------------------------------------------------------

void Screen::init()
{
  Serial.println("[screen] initialisation de la carte");
  Serial.flush();

  s_board = new Board();
  s_board->init();

  auto lcd = s_board->getLCD();

  if (lcd) {
    // Un seul framebuffer : on dessine directement dedans (pas de buffer
    // intermediaire, la dalle lit le framebuffer en PSRAM en continu).
    lcd->configFrameBufferNumber(1);

    // Bounce buffer : la dalle ne lit plus le framebuffer PSRAM en direct.
    // Le controleur RGB en copie des tranches dans une petite memoire
    // interne (10 lignes), ce qui la rend insensible aux acces concurrents
    // au bus PSRAM. Sans lui, des que le WiFi emet, le DMA de l'ecran est
    // retarde : l'image se decale vers le haut et des lignes clignotent.
    auto bus = lcd->getBus();

    if (bus) {
      BusRGB *rgb = static_cast<BusRGB *>(bus);

      // Bounce buffer : la dalle ne lit plus le framebuffer PSRAM en direct,
      // le controleur RGB en copie des tranches dans une petite memoire
      // interne. Plus la tranche est grande, plus la dalle tolere une
      // longue pause du bus (WiFi) sans perdre la synchronisation : la
      // carte regle 10 lignes, on monte a 30 (48 Ko de RAM interne).
      s_pclk   = PANEL_PCLK_HZ;

      s_bounce = rgb->configRGB_BounceBufferSize(PANEL_W * PANEL_BOUNCE_LINES);

      // Libere les lignes de cache apres chaque copie PSRAM -> RAM interne.
      // Sans cela le framebuffer sature le cache et la pile WiFi, qui a
      // besoin du meme bus, prend le dessus : l'image se decale et des
      // lignes clignotent. L'accesseur est marque obsolete dans la
      // bibliotheque, mais c'est le seul qui expose la configuration.
      auto *cfg = const_cast<esp_lcd_rgb_panel_config_t *>(rgb->getRgbConfig());

      if (cfg) {
        cfg->flags.bb_invalidate_cache = 1;

        // Allonge le blanking vertical : la dalle passe de ~39 a ~22
        // images/s. Le bus memoire reste libre plus longtemps entre deux
        // images, ce qui laisse au WiFi le loisir de travailler sans
        // retarder la dalle.
        cfg->timings.vsync_back_porch = PANEL_VBP_LINES;
      }

      ESP_LOGI("screen", "bounce buffer de %d lignes : %s", PANEL_BOUNCE_LINES, s_bounce ? "ok" : "refuse");
    }

    Serial.printf("[screen] dalle %dx%d\n", lcd->getFrameWidth(), lcd->getFrameHeight());
    Serial.flush();
  }

  if (!s_board->begin()) {
    Serial.println("[screen] board->begin() a echoue !");
    Serial.flush();
  }

  if (lcd) {
    s_frame = (uint16_t *)lcd->getFrameBufferByIndex(0);
    s_lcd   = lcd;
  }
  s_touch = s_board->getTouch();

  // Surveillance du balayage (voir onFrameComplete) : ne perturbe rien, la
  // copie des donnees reste assuree par le pilote.
  registerFrameCallback();

  Serial.printf("[screen] framebuffer = %p / tactile = %p\n", s_frame, s_touch);

  //-------------------------------------------------------------------
  // Canvas de l'interface : taille native de la dalle (800x480)
  //-------------------------------------------------------------------

  setColorDepth(16);

  void *buffer = createSprite(UI_W, UI_H);
  Serial.printf("[screen] canvas %dx%d = %p (PSRAM attendu)\n", UI_W, UI_H, buffer);
  fillSprite(TFT_BLACK);

  clearPanel();
  setBacklight(true);
  flush();

  s_ready = (s_frame != nullptr);
}

//---------------------------------------------------------------------

void Screen::clearPanel()
{
  if (!s_frame) return;

  uint32_t size = (uint32_t)PANEL_W * PANEL_H;

  for (uint32_t i = 0; i < size; i++) s_frame[i] = TFT_BLACK;
}

//---------------------------------------------------------------------
// Recopie du canvas dans le framebuffer de la dalle (1:1)
//
// Chaque ligne du canvas correspond a la meme ligne de la dalle : une
// simple copie avec inversion des octets (TFT_eSPI stocke ses sprites en
// "big endian", la dalle RGB attend du RGB565 natif).
//
// La copie est decoupee : pendant qu'elle tourne, le processeur sature le
// bus PSRAM et la dalle ne peut pas recharger ses tampons internes. Une
// copie d'un seul trait (768 Ko, une vingtaine de millisecondes) garantit
// un decrochage. Par tranches de 8 lignes (16 Ko, ~0,3 ms) suivies d'une
// milliseconde de repos, la dalle reprend son souffle entre chaque.
//---------------------------------------------------------------------

#define FLUSH_ROWS 8

void Screen::flush()
{
  uint16_t *source = (uint16_t *)getPointer();

  if (!s_frame || !source) return;

  for (int32_t first = 0; first < UI_H; first += FLUSH_ROWS) {
    int32_t last = first + FLUSH_ROWS;

    if (last > UI_H) last = UI_H;

    for (int32_t y = first; y < last; y++) {
      const uint16_t *src = source + (int32_t)y * UI_W;
      uint16_t       *dst = s_frame + (int32_t)(y + UI_OFF_Y) * PANEL_W + UI_OFF_X;

      for (int32_t x = 0; x < UI_W; x++)
        dst[x] = (uint16_t)((src[x] >> 8) | (src[x] << 8));
    }

    delay(1);                     // laisse la dalle recharger ses tampons
  }
}

//---------------------------------------------------------------------
// Tactile (GT911) : les coordonnees sont deja celles du canvas
//---------------------------------------------------------------------

uint8_t Screen::getTouch(uint16_t *x, uint16_t *y, uint16_t threshold)
{
  (void)threshold;

  if (!s_touch) return 0;

  TouchPoint point;

  if (s_touch->readPoints(&point, 1, 0) <= 0) return 0;

  int32_t canvas_x = (int32_t)point.x - UI_OFF_X;
  int32_t canvas_y = (int32_t)point.y - UI_OFF_Y;

  if (canvas_x < 0 || canvas_y < 0 || canvas_x >= UI_W || canvas_y >= UI_H) return 0;

  *x = (uint16_t)canvas_x;
  *y = (uint16_t)canvas_y;

  return 1;
}

//---------------------------------------------------------------------

//---------------------------------------------------------------------

bool Screen::bounceBufferActive()
{
  return s_bounce;
}

//---------------------------------------------------------------------

int Screen::pixelClockHz()
{
  return s_pclk;
}

//---------------------------------------------------------------------
// Change la frequence pixel a chaud : plus elle est basse, plus la dalle
// consomme lentement les donnees, donc plus elle tolere une pause du bus.
// Renvoie le code d'erreur du pilote (0 = accepte).
//---------------------------------------------------------------------

int Screen::setPixelClock(uint32_t hz)
{
  if (!s_lcd) return -1;

  esp_err_t err = esp_lcd_rgb_panel_set_pclk(s_lcd->getRefreshPanelHandle(), hz);

  if (err == ESP_OK) s_pclk = (int)hz;

  return (int)err;
}

//---------------------------------------------------------------------

void Screen::frameStats(uint32_t *frames, uint32_t *stalls, uint32_t *maxUs)
{
  if (frames) *frames = s_frames;
  if (stalls) *stalls = s_stalls;
  if (maxUs)  *maxUs  = s_maxUs;
}

//---------------------------------------------------------------------

bool Screen::ready()
{
  return s_ready;
}

//---------------------------------------------------------------------
// Demande au controleur RGB de repartir du debut de l'image au prochain
// VSYNC : remet l'affichage en place s'il a perdu la synchronisation
// (image decalee apres une rafale du WiFi).
//---------------------------------------------------------------------

void Screen::restartPanel()
{
  if (s_lcd) s_restartErr = (int)esp_lcd_rgb_panel_restart(s_lcd->getRefreshPanelHandle());
}

//---------------------------------------------------------------------
// Reinitialisation complete de la dalle, sans redemarrer la carte.
//
// del() detruit le pilote RGB (transfert arrete, framebuffer libere) et
// begin() le recree : controleur reinitalise, impulsion de reset envoyee a
// la dalle, nouveau framebuffer. C'est cette impulsion qui remet l'image en
// place ; le reste du programme (mesures, historique, WiFi) n'est pas
// touche, contrairement a un redemarrage.
//---------------------------------------------------------------------

bool Screen::reinit()
{
  if (!s_lcd) return false;

  ESP_LOGI("screen", "reinitialisation de la dalle (sans redemarrage)");

  if (!s_lcd->del()) {
    ESP_LOGE("screen", "del() a echoue");
    return false;
  }

  if (!s_lcd->begin()) {
    ESP_LOGE("screen", "begin() a echoue");
    return false;
  }

  void *fb = s_lcd->getFrameBufferByIndex(0);

  if (!fb) {
    ESP_LOGE("screen", "framebuffer absent apres reinitialisation");
    return false;
  }

  s_frame = (uint16_t *)fb;

  registerFrameCallback();      // begin() a repose son propre callback

  // begin() repart d'un framebuffer vierge : on y recopie l'interface
  // courante, qui est intacte dans le canvas.
  if (s_self) {
    s_self->clearPanel();
    s_self->flush();
  }

  return true;
}

//---------------------------------------------------------------------

int Screen::restartError()
{
  return s_restartErr;
}

//---------------------------------------------------------------------
// Retro eclairage
//
// Sur cette carte, le retro eclairage n'est pas une sortie du
// microcontroleur mais la sortie EXIO2 du CH422G (0x20), active a 1 :
// sans cette sortie, la dalle s'allume quand meme (les sorties du
// CH422G sont a 1 a la mise sous tension) mais plus rien ne peut
// l'eteindre. On passe donc par l'objet Backlight de la bibliotheque,
// et si celui-ci n'a pas pu demarrer (ou si la sortie est restee en
// entree), on force directement la sortie de l'expandeur.
//---------------------------------------------------------------------

#define BACKLIGHT_EXIO        2

// Le CH422G n'a pas de registre d'adresse : chaque registre est une
// adresse I2C a part entiere.
#define CH422G_REG_WR_SET     0x24    // (0x48 >> 1) direction/options (bit 0 = IO_OE)
#define CH422G_REG_RD_IO      0x26    // (0x4D >> 1) relecture des entrees IO
#define CH422G_REG_WR_IO      0x38    // (0x70 >> 1) sorties IO0..IO7

// Ecriture directe d'un registre du CH422G (un octet apres l'adresse).
static bool ch422Write(uint8_t reg, uint8_t value)
{
  boardI2C.beginTransmission(reg);
  boardI2C.write(value);

  return boardI2C.endTransmission() == 0;
}

static int ch422ReadIO()
{
  // beginTransmission() remet a zero la longueur d'ecriture en attente :
  // sans cela, requestFrom() enverrait un octet au composant (et ici un
  // octet sur l'adresse de lecture du CH422G bloque le bus I2C).
  boardI2C.beginTransmission(CH422G_REG_RD_IO);

  if (boardI2C.requestFrom(CH422G_REG_RD_IO, 1) != 1) return -1;

  return boardI2C.read();
}

void Screen::setBacklight(bool on)
{
  _backlightOn = on;

  if (!s_board) return;

  // 1) Voie normale : l'objet Backlight de la carte connait le niveau
  //    actif et tient a jour la copie interne du pilote.
  auto backlight = s_board->getBacklight();
  bool ok = backlight ? (on ? backlight->on() : backlight->off()) : false;

  // 2) Filet de securite : ecriture directe des registres du CH422G,
  //    apres la precedente pour ne pas desynchroniser le pilote.
  //
  //    WR_SET = 0x01 est indispensable : sans ce bit IO_OE les sorties
  //    IO de l'expandeur restent en haute impedance, la sortie EXIO2 ne
  //    suit plus WR_IO (RD_IO renvoie 0xFF) et le retro eclairage reste
  //    allume par le pull-up. Verifie sur la carte : avec cette
  //    ecriture, RD_IO = 0xFB et l'ecran s'eteint.
  bool oe = ch422Write(CH422G_REG_WR_SET, 0x01);
  bool io = ch422Write(CH422G_REG_WR_IO, on ? 0xFF : (uint8_t)~(1 << BACKLIGHT_EXIO));

  Serial.printf("[screen] retroeclairage %s (bibliotheque %s, IO_OE %s, WR_IO %s, RD_IO = 0x%02X)\n",
                on ? "ON" : "OFF",
                ok ? "ok" : "en echec",
                oe ? "ok" : "en echec",
                io ? "ok" : "en echec",
                ch422ReadIO());
}
