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
#define DARK_THEME_BACKGROUND    QColor(30, 41, 59)    /* #1e293b slate-800 */
#define DARK_THEME_CARD          QColor(51, 65, 85)    /* #334155 slate-700 */
#define DARK_THEME_TEXT          QColor(241, 245, 249) /* #f1f5f9 slate-100 */
#define DARK_THEME_TEXT_MUTED    QColor(148, 163, 184) /* #94a3b8 slate-400 */
#define DARK_THEME_ACCENT        QColor(56, 189, 248)  /* #38bdf4 sky-400 */
#define DARK_THEME_BORDER        QColor(71, 85, 105)    /* #475569 slate-600 */
#define DARK_THEME_ALERT_BG      QColor(34, 211, 238)  /* #22d3ee cyan-400 */
#define DARK_THEME_ALERT_TEXT    QColor(15, 23, 42)    /* #0f172a slate-900 */

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
