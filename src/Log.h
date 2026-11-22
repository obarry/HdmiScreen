// =============================================================================
// Log.h — Journalisation minimale
// =============================================================================
//
// Une application « fenêtrée » (sous-système WINDOWS) n'a pas de console :
// printf n'affiche rien. On écrit donc :
//  - toujours dans le flux de débogage (visible dans Visual Studio, fenêtre
//    « Sortie », ou avec l'outil DebugView de Sysinternals) ;
//  - dans la console de l'invite de commandes qui a lancé le programme, si
//    attachParentConsole() a réussi (utile pour --list et --help).

#pragma once

#include <string_view>

namespace hdmi::log {   // [C++17] namespaces imbriqués en une ligne

// Se raccroche à la console du processus parent (cmd.exe, PowerShell).
// Renvoie false si le programme a été lancé par double-clic.
bool attachParentConsole();

// Vrai si une console est disponible pour l'affichage.
bool hasConsole();

// [C++] std::wstring_view : « vue » en lecture seule sur une chaîne existante,
// sans copie. Accepte aussi bien un std::wstring qu'un littéral L"...".
void info(std::wstring_view message);
void error(std::wstring_view message);

// Écrit sur la console uniquement (sans préfixe), pour les sorties de --list.
void print(std::wstring_view text);

} // namespace hdmi::log
