// sim stub of shared/web_panel.h: the GTi's web page needs a real network; in the simulator it never comes up.
#pragma once
static bool g_web_up = false;
static void webPanelBegin() { Serial.println("[WEB] web page: not available in the simulator"); }
static void webPanelBeginAP() { Serial.println("[WEB] GTi Wi-Fi: not available in the simulator"); }
static void webPanelService() {}
static void webPanelStop() { g_web_up = false; }
