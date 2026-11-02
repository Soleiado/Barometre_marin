//=====================================================================
//  theme.h - palette du theme "instrument marin"
//
//  Fond sombre, accent teal, aiguille ambre, typographie claire : un
//  aspect d'instrument de navigation moderne (type chartplotter).
//
//  Les valeurs sont des couleurs RGB565 precalculees (TFT_eSPI::color565
//  n'est pas statique, on ne peut pas l'appeler dans un #define).
//=====================================================================

#ifndef _INC_THEME_H
#define _INC_THEME_H

// Fond general (sombre, legerement bleute)        #0b1420
#define THEME_BG      0x08A4
// Panneaux / cadrans                              #132331
#define THEME_PANEL   0x1106
// Piste des cadrans et grille                     #1c3247 / #253a52
#define THEME_TRACK   0x1988
#define THEME_GRID    0x21CA
// Accent teal (courbes, titres)                   #2dd4bf
#define THEME_ACCENT  0x2EB7
// Aiguille / alerte                               #f59e0b
#define THEME_AMBER   0xF4E1
// Texte                                           #e8eef5 / #7d95ab
#define THEME_TEXT    0xEF7E
#define THEME_DIM     0x7CB5
// Ligne de reference 1013 hPa                     #34d399
#define THEME_GREEN   0x3693

// Zones des cadrans (couleurs attenuees)
#define THEME_COLD    0x3C1E   // froid (bleu)      #3b82f6
#define THEME_MID     0x9517   // neutre / confort  #94a3b8
#define THEME_WARM    0xF4E1   // tiede (ambre)     #f59e0b
#define THEME_HOT     0xEA28   // chaud / trop (rouge) #ef4444
#define THEME_OK      0x262B   // confort (vert)    #22c55e

#endif // _INC_THEME_H
