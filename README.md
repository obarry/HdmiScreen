# HdmiScreen

Utilitaire Windows 11 qui affiche **en plein écran l'entrée d'un boîtier de capture HDMI**. Il transforme un portable personnel en second écran d'un autre ordinateur (par exemple un portable professionnel qui n'accepte ni USB ni logiciel non approuvé) :

```
Portable pro ──HDMI──▶ Boîtier de capture HDMI→USB ──USB 3──▶ Portable perso + HdmiScreen
```

Pour le portable pro, le boîtier est un simple écran HDMI. Sur le portable perso, HdmiScreen affiche l'image reçue en plein écran, sans bordure, avec une latence minimale.

- **C++20 natif** : Win32, Media Foundation, Direct3D 11. Aucune dépendance externe.
- **Un seul `.exe`**, sans installation.
- **Aucun réglage** dans le cas courant : choix automatique du boîtier et du meilleur mode pour l'écran.
- Code et documentation **pédagogiques**, pensés pour un développeur venant de Java.

## Prérequis

| Élément | Détail |
| --- | --- |
| Système | Windows 10 (1703+) ou Windows 11, 64 bits |
| Boîtier | Boîtier de capture HDMI → USB de classe UVC (reconnu par l'appli Caméra), branché sur un port **USB 3** |
| Pour compiler | Visual Studio 2022 avec la charge de travail « Développement Desktop en C++ » (inclut MSVC, le SDK Windows et CMake) |

## Compiler

**Avec Visual Studio 2022** : *Fichier > Ouvrir > Dossier…*, choisir le dossier du projet. Visual Studio détecte `CMakeLists.txt`. Choisir la configuration `x64-Release`, puis la cible `hdmiscreen.exe`, et lancer.

**En ligne de commande** (« Developer PowerShell for VS 2022 ») :

```powershell
cmake -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
ctest --test-dir build -C Release        # tests de la logique de choix
```

L'exécutable est produit dans `build\Release\hdmiscreen.exe`.

## Utiliser

1. Brancher le boîtier sur un port USB 3 du portable perso, et le câble HDMI du portable pro.
2. Sur le portable pro : Win+P > **Étendre**, puis Paramètres > Affichage. Sélectionner le nouvel écran et choisir la résolution du mode capturé (voir `--list`), de préférence **1920×1200** si elle est proposée, sinon **1920×1080**. Mettre l'échelle à 100 %.
3. Sur le portable perso : lancer `hdmiscreen.exe`.

| Touche | Action |
| --- | --- |
| Échap ou Alt+F4 | Quitter |
| F11 | Basculer plein écran / fenêtre |
| S | Mise à l'échelle : ajuster → 1:1 → étirer |

Le curseur se masque après 2 s d'immobilité, et la mise en veille de l'écran est bloquée tant que l'outil tourne.

**Coupures.** Si le boîtier disparaît un instant (faux contact USB), la dernière image reste affichée et l'outil se reconnecte tout seul au même boîtier dès qu'il réapparaît. Au-delà de 3 s sans image, un écran gris sombre s'affiche. La webcam du portable n'est jamais choisie automatiquement ; pour l'utiliser malgré tout, il faut la désigner avec `--device`.

### Options

| Option | Effet |
| --- | --- |
| `--list` | Liste les périphériques vidéo, leurs modes et les choix automatiques, puis quitte |
| `--device "texte"` | Utilise le périphérique dont le nom contient ce texte (même une webcam) |
| `--mode 1920x1200[@60]` | Impose un mode de capture |
| `--scaling fit\|1to1\|stretch` | Mise à l'échelle initiale (défaut : `fit`) |
| `--full-range` | Le signal est en plage complète 0-255 (à essayer si l'image paraît trop contrastée) |
| `--windowed` | Démarre en fenêtre |
| `--help` | Aide |

Premier lancement conseillé, depuis une invite de commandes :

```powershell
.\hdmiscreen.exe --list
```

## Structure du projet

```
HdmiScreen/
├─ CMakeLists.txt          description du build
├─ src/
│  ├─ main.cpp             point d'entrée
│  ├─ App.*                assemblage, boucle principale, --list
│  ├─ MainWindow.*         fenêtre Win32
│  ├─ CaptureDevices.*     découverte des périphériques et modes (Media Foundation)
│  ├─ CaptureSession.*     capture asynchrone (objet COM)
│  ├─ Renderer.*           affichage (Direct3D 11)
│  ├─ ComUtils.h, Log.*    outils communs
│  └─ VideoMode.h, Selection.*, Geometry.h, CommandLine.*   logique pure, testée
├─ tests/SelectionTests.cpp
└─ docs/
   ├─ ARCHITECTURE.md      threads, trajet d'une image, choix du mode, erreurs
   ├─ API_WINDOWS.md       Win32, COM, Media Foundation, Direct3D 11 (et ce que c'est)
   └─ CPP_POUR_DEV_JAVA.md le C++ du projet expliqué à un développeur Java
```

## Lire le code

Ordre de lecture conseillé :

1. [docs/CPP_POUR_DEV_JAVA.md](docs/CPP_POUR_DEV_JAVA.md), au moins les §0 à §4 ;
2. `VideoMode.h`, `Selection.cpp` et les tests : du C++ pur, sans Windows ;
3. [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md), puis `main.cpp` et `App.cpp` ;
4. `MainWindow.cpp`, avec [docs/API_WINDOWS.md](docs/API_WINDOWS.md) §1 ;
5. `CaptureDevices.cpp` et `CaptureSession.cpp`, avec API_WINDOWS §2 et §3 ;
6. `Renderer.cpp`, avec API_WINDOWS §4 et §5.

Les commentaires du code sont balisés :

| Balise | Signification |
| --- | --- |
| `// [C++]` | Point de langage C++ |
| `// [C++ vs Java]` | Différence notable avec Java |
| `// [API]` | Appel ou notion d'API Windows, avec le nom à chercher dans la documentation Microsoft |

## Feuille de route

| Étape | Contenu | État |
| --- | --- | --- |
| 1 | Validation du matériel (appli Caméra) | Fait |
| 2 | Prototype : liste des modes, capture, affichage plein écran | **Cette version** |
| 3 | Choix automatique du mode, faible latence | **Cette version** |
| 4 | Confort : mise à l'échelle, curseur, anti-veille, reconnexion | **Cette version** (sauf fichier .ini) |
| 5 | Texte à l'écran (F1), fichier `hdmiscreen.ini`, agrandissement par shader | À faire |
| 6 | Optionnel : audio HDMI, icône | À faire |

Cette version a été compilée et ses tests exécutés, mais pas encore essayée avec le boîtier : la première session de test sur le matériel validera la capture, le rendu et les modes réels.
