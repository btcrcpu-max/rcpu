// Copyright (c) 2011-2022 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QT_GUICONSTANTS_H
#define BITCOIN_QT_GUICONSTANTS_H

#include <chrono>
#include <cstdint>

using namespace std::chrono_literals;

/* A delay between model updates */
static constexpr auto MODEL_UPDATE_DELAY{250ms};

/* A delay between shutdown pollings */
static constexpr auto SHUTDOWN_POLLING_DELAY{200ms};

/* AskPassphraseDialog -- Maximum passphrase length */
static const int MAX_PASSPHRASE_SIZE = 1024;

/* BitcoinGUI -- Size of icons in status bar */
static const int STATUSBAR_ICONSIZE = 16;

static const bool DEFAULT_SPLASHSCREEN = true;

/* Invalid field background style */
#define STYLE_INVALID "border: 3px solid #FF8080"

/* Transaction list -- unconfirmed transaction */
#define COLOR_UNCONFIRMED QColor(128, 128, 128)
/* Transaction list -- negative amount */
#define COLOR_NEGATIVE QColor(255, 0, 0)
/* Transaction list -- bare address (without label) */
#define COLOR_BAREADDRESS QColor(140, 140, 140)
/* Transaction list -- TX status decoration - danger, tx needs attention */
#define COLOR_TX_STATUS_DANGER QColor(200, 100, 100)
/* Transaction list -- TX status decoration - default color (light in dark theme) */
#define COLOR_BLACK QColor(250, 250, 250)

/* !RCPU Dark Theme -- core palette */
#define DARK_THEME_BACKGROUND    QColor(24, 24, 27)    /* #18181b zinc-950 */
#define DARK_THEME_CARD          QColor(39, 39, 42)    /* #27272a zinc-800 */
#define DARK_THEME_TEXT          QColor(250, 250, 250) /* #fafafa zinc-50 */
#define DARK_THEME_TEXT_MUTED    QColor(161, 161, 170) /* #a1a1aa zinc-400 */
#define DARK_THEME_ACCENT        QColor(249, 115, 22)  /* #f97316 orange-500 */
#define DARK_THEME_BORDER        QColor(63, 63, 70)     /* #3f3f46 zinc-600 */
#define DARK_THEME_ALERT_BG      QColor(251, 191, 36)  /* #fbbf24 amber-400 */
#define DARK_THEME_ALERT_TEXT    QColor(0, 0, 0)        /* #000000 */

/* Tooltips longer than this (in characters) are converted into rich text,
   so that they can be word-wrapped.
 */
static const int TOOLTIP_WRAP_THRESHOLD = 80;

/* Number of frames in spinner animation */
#define SPINNER_FRAMES 36

// !RCPU
#define QAPP_ORG_NAME "RCPU"
#define QAPP_ORG_DOMAIN "https://github.com/rcpu-project/"
#define QAPP_APP_NAME_DEFAULT "RCPU-Qt"
#define QAPP_APP_NAME_RCPU_TESTNET "RCPU-Qt-testnet"
#define QAPP_APP_NAME_RCPU_REGTEST "RCPU-Qt-regtest"
#define QAPP_APP_NAME_BTC "Bitcoin-Qt"
// !RCPU END
#define QAPP_APP_NAME_TESTNET "Bitcoin-Qt-testnet"
#define QAPP_APP_NAME_SIGNET "Bitcoin-Qt-signet"
#define QAPP_APP_NAME_REGTEST "Bitcoin-Qt-regtest"

/* One gigabyte (GB) in bytes */
static constexpr uint64_t GB_BYTES{1000000000};

// Default prune target displayed in GUI.
static constexpr int DEFAULT_PRUNE_TARGET_GB{2};

#endif // BITCOIN_QT_GUICONSTANTS_H
