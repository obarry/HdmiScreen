# Le C++ de HdmiScreen, expliqué à un développeur Java

Ce guide reprend les points où le C++ diffère **sensiblement** de Java, du plus structurant au plus subtil. Chaque notion est illustrée par du code du projet. Dans les sources, les commentaires `// [C++]` et `// [C++ vs Java]` signalent ces mêmes points là où ils apparaissent, et renvoient ici par leur numéro de section (`§3`…).

Le projet utilise **C++20** : la version moderne du langage, très différente du C++ « à la C » des années 1990. Il ne contient ni `new`/`delete` manuels (sauf une fois, imposé par COM), ni `malloc`, ni macros, hormis dans les tests.

---

## 0. Correspondances rapides

| Java | C++ (dans ce projet) | Section |
| --- | --- | --- |
| `package` | `namespace hdmi { … }` | §12 |
| `import` | `#include "Fichier.h"` + `using` | §1 |
| Classe `.java` | Déclaration `.h` + implémentation `.cpp` | §1 |
| Maven / Gradle | CMake (génère le projet, ne télécharge rien) | §1, §14 |
| Référence d'objet | Valeur, référence `&`, pointeur `*`, pointeur intelligent | §2, §4 |
| Ramasse-miettes | Destructeurs + RAII + comptage de références | §3, §4 |
| `try-with-resources` | RAII, automatique sur toute variable locale | §3 |
| `final` (variable) | `const` | §2 |
| `ArrayList<T>` | `std::vector<T>` | §8 |
| `Optional<T>` | `std::optional<T>` | §8 |
| `String` (UTF-16) | `std::wstring` sous Windows ; `std::string` = octets | §7 |
| `String.format` | `std::format` | §7 |
| Lambda + interface fonctionnelle | Lambda avec liste de capture `[…]` + `std::function` | §8 |
| `interface` | Classe à méthodes virtuelles pures (`= 0`) | §9 |
| `@Override` | `override` | §9 |
| `synchronized` | `std::mutex` + `std::lock_guard` | §11 |
| `AtomicInteger` / `volatile` | `std::atomic<T>` | §11 |
| `RuntimeException` | Toute exception (pas d'exceptions vérifiées) | §6 |
| `instanceof` + cast | `dynamic_cast` (C++), `QueryInterface` / `ComPtr::As` (COM) | §9 |
| `int` toujours 32 bits | `int32_t`, `uint32_t`… pour une taille garantie | §12 |
| `public static void main` | Fonction libre `wWinMain` | §1 |
| Surcharges pour simuler des valeurs par défaut | Arguments par défaut (`bool checked = false`) | §9 |
| `switch` sur une `String` | `switch` uniquement sur entiers et enums | §12 |

---

## 1. Le modèle de compilation : en-têtes, sources, éditeur de liens

**C'est la différence la plus déroutante au début.** Le compilateur Java lit vos classes et retrouve tout seul les autres via le classpath. Le compilateur C++, lui, traite **chaque `.cpp` isolément** (une *unité de traduction*) et ne connaît que ce qu'on lui a montré au-dessus, ligne par ligne.

D'où le découpage en deux fichiers par classe :

- le **`.h` (en-tête)** contient les **déclarations** : « il existe une classe `Renderer` avec telles méthodes ». C'est un contrat, comme une interface ;
- le **`.cpp`** contient les **définitions** : le corps des méthodes.

`#include "Renderer.h"` est un **copier-coller textuel** du fichier, fait avant la compilation par le *préprocesseur*. Ce n'est pas un `import` : tout le contenu est réellement inséré. D'où `#pragma once` en tête de chaque `.h`, qui empêche qu'un même en-tête soit collé deux fois dans un `.cpp`.

Ensuite, l'**éditeur de liens** (*linker*) assemble les fichiers objets (`.obj`) et les bibliothèques (`d3d11.lib`, `mfplat.lib`…) en un `.exe`. Une erreur « *unresolved external symbol* » signifie : déclaré dans un `.h` mais défini nulle part (fichier `.cpp` oublié dans `CMakeLists.txt`, bibliothèque non liée…). En Java, ces erreurs n'existent pas : c'est la JVM qui charge les classes à l'exécution.

Conséquences pratiques :

- **l'ordre compte** : on ne peut utiliser un nom qu'après sa déclaration ;
- **n'inclure que le nécessaire** dans un `.h`, pour ne pas ralentir la compilation de tous ceux qui l'incluent ;
- une fonction **définie** dans un `.h` doit être `inline` (ex. `toString(PixelFormat)` dans `VideoMode.h`), sinon chaque `.cpp` qui l'inclut en aurait une copie et l'éditeur de liens signalerait un doublon ;
- les **fonctions libres** (hors de toute classe) sont normales en C++ : pas besoin d'une classe utilitaire statique. Le point d'entrée lui-même, `wWinMain`, est une fonction libre.

**CMake** décrit la liste des sources et des bibliothèques. Il ne compile pas lui-même : il génère un projet Visual Studio (ou des fichiers Ninja/make). Il ne télécharge aucune dépendance : ici, tout vient de Windows et de son SDK.

---

## 2. Valeurs, références et pointeurs

En Java, une variable de type objet est **toujours une référence**. En C++, par défaut, **une variable EST l'objet**.

```cpp
VideoMode a;          // l'objet entier est ici, sur la pile (stack) : pas de new
VideoMode b = a;      // COPIE de tous les champs : a et b sont indépendants
b.width = 1280;       // a.width n'a pas changé
```

En Java, `b = a` ferait pointer les deux variables vers le même objet. Il existe trois façons de désigner un objet sans le copier :

| Écriture | Nom | Peut être nul ? | Peut changer de cible ? | Usage dans le projet |
| --- | --- | --- | --- | --- |
| `const VideoMode& m` | Référence | Non | Non | Paramètres en lecture : `describe(const VideoMode& mode)` |
| `VideoMode& m` | Référence modifiable | Non | Non | Paramètre de sortie : `clientSize(uint32_t& width, …)` |
| `IMFSample* s` | Pointeur brut | Oui (`nullptr`) | Oui | Paramètres des API Windows, pointeurs « non propriétaires » |

**Règle de passage des paramètres** (à appliquer par réflexe) :

- petits types (`int`, `bool`, enum, petite struct comme `ScreenSize`) : **par valeur** ;
- objets plus gros (`std::wstring`, `std::vector`, `VideoMode`) : **par référence constante** `const T&`. Sans le `&`, le vecteur entier serait copié à chaque appel ;
- objet que la fonction doit modifier : par référence `T&` (ou en valeur de retour, souvent plus lisible).

**`const`** est bien plus présent qu'en Java :

- `const VideoMode& m` : la fonction promet de ne pas modifier `m` ;
- `double fps() const` : cette **méthode** ne modifie pas l'objet. Sur un objet `const`, seules les méthodes `const` sont appelables. Il n'y a pas d'équivalent en Java ;
- `const auto x = …` : comme `final` sur une variable locale.

**Initialiser toujours.** Un `int` local non initialisé contient une valeur aléatoire (et le lire est un *comportement indéfini*, §13). Le projet initialise chaque champ à sa déclaration (`uint32_t width = 0;`) et chaque structure Windows avec `{}` (`DXGI_SWAP_CHAIN_DESC1 desc{};` met tout à zéro).

---

## 3. Durée de vie des objets et RAII

**Il n'y a pas de ramasse-miettes.** En échange, la destruction est **déterministe** : un objet local est détruit à la sortie de son bloc `{ }`, toujours, y compris quand une exception est levée. À ce moment, son **destructeur** (`~NomDeClasse()`) s'exécute.

Le C++ en tire son idiome central, le **RAII** (*Resource Acquisition Is Initialization*) : toute ressource (mémoire, handle, verrou, initialisation d'une bibliothèque) est confiée à un objet dont le constructeur l'acquiert et dont le destructeur la libère.

```cpp
// main.cpp
{
    const hdmi::ComInitializer com;                   // CoInitializeEx
    const hdmi::MediaFoundationInitializer mediaFoundation;   // MFStartup
    hdmi::App app(instance, options);
    return app.run();
}   // destruction dans l'ordre INVERSE : app, puis MFShutdown, puis CoUninitialize
```

C'est un `try-with-resources` implicite et généralisé : pas de `finally`, pas d'oubli possible. Les classes RAII du projet :

| Classe | Ressource |
| --- | --- |
| `ComInitializer`, `MediaFoundationInitializer` | Initialisation de COM et de MF |
| `UniqueHandle` | Un `HANDLE` Windows (`CloseHandle`) |
| `CoTaskString` | Une chaîne allouée par COM (`CoTaskMemFree`) |
| `LockedBuffer` (`Renderer.cpp`) | Le verrou d'un tampon MF (`Unlock`) |
| `KeepDisplayOn` (`App.cpp`) | L'interdiction de mise en veille |
| `std::lock_guard` | Un mutex (§11) |
| `ComPtr`, `std::unique_ptr` | Un objet COM ou alloué (§4) |

**Ordre de destruction des membres.** Les membres d'une classe sont construits dans l'ordre de leur **déclaration** et détruits dans l'ordre inverse. `App.h` en tire parti : `window_` est déclaré avant `renderer_`, lui-même avant `session_`. La capture s'arrête donc avant le rendu, et le rendu avant la fenêtre.

**Copier un objet qui possède une ressource est dangereux** : deux copies d'un `UniqueHandle` fermeraient deux fois le même handle. On interdit donc la copie :

```cpp
UniqueHandle(const UniqueHandle&) = delete;             // pas de constructeur de copie
UniqueHandle& operator=(const UniqueHandle&) = delete;  // pas d'affectation par copie
```

La question ne se pose jamais en Java, où l'on ne copie que des références.

---

## 4. Pointeurs intelligents : qui possède quoi

En C++ moderne, on n'écrit (presque) jamais `delete`. La question à se poser pour chaque pointeur est : **qui est le propriétaire, c'est-à-dire qui détruira l'objet ?**

| Type | Propriété | Libération | Dans le projet |
| --- | --- | --- | --- |
| `std::unique_ptr<T>` | Un seul propriétaire, non copiable (déplaçable) | `delete` automatique | `App::renderer_` |
| `std::shared_ptr<T>` | Partagée, compteur de références | Au dernier propriétaire | (non utilisé) |
| `ComPtr<T>` | Partagée, compteur **COM** (`AddRef`/`Release`) | `Release` automatique | Tous les objets COM |
| `T*` brut | **Aucune** : simple observateur | Jamais par ce pointeur | Paramètres d'API, `LockedBuffer::data_` |

`std::make_unique<Renderer>(hwnd)` crée l'objet et son `unique_ptr` en une fois (cf. `App::App`).

`ComPtr` ressemble à un `shared_ptr`, mais le compteur est **dans l'objet COM** lui-même : c'est la convention de Windows. Ses opérations sont détaillées dans [API_WINDOWS.md §2.5](API_WINDOWS.md#25-comptr--le-pointeur-intelligent-com).

**Pointeur brut ≠ propriétaire.** Quand une fonction reçoit `IMFMediaSource* source`, elle l'utilise mais ne le libère pas. C'est l'appelant qui en reste propriétaire. C'est la convention de toutes les signatures du projet.

**Cycles.** Comme `shared_ptr`, le comptage de références ne sait pas libérer des objets qui se référencent mutuellement. Le cas se présente avec la session de capture ; il est expliqué dans [API_WINDOWS.md §2.4](API_WINDOWS.md#24-implémenter-un-objet-com--capturesession).

---

## 5. La sémantique de déplacement (`std::move`)

Puisque les objets se copient par défaut, renvoyer ou ranger un gros objet pourrait coûter cher. Le C++11 a ajouté le **déplacement** : au lieu de copier le contenu, on « vole » les ressources de l'objet source, qui reste vide mais valide.

```cpp
// CaptureSession::takeLatestSample
return std::move(latestSample_);   // transfère le pointeur : pas d'AddRef/Release,
                                   // latestSample_ devient vide

// App.cpp, probeDevices
probed.push_back(std::move(device));   // le ProbedDevice est déplacé dans le vecteur
```

`std::move` ne déplace rien par lui-même : il **autorise** le déplacement en marquant l'objet comme « je n'en ai plus besoin ». Après un `std::move`, on ne lit plus la variable (sauf pour lui réaffecter une valeur).

**Retourner une variable locale par valeur est gratuit** : `return result;` dans `usableModes` ne copie pas le vecteur (le compilateur le construit directement chez l'appelant, ou le déplace). Il ne faut donc pas écrire `return std::move(result);`, qui peut même empêcher cette optimisation.

**Recevoir par valeur, puis déplacer.** Quand une fonction doit **garder** une chaîne (la stocker dans un membre), on la reçoit par valeur et on la déplace dans le membre :

```cpp
// App.cpp
App::App(HINSTANCE instance, const Options& options, std::wstring settingsPath)
    : …, settingsPath_(std::move(settingsPath)) { }
```

L'appelant paie une seule copie s'il garde sa chaîne, et aucune s'il passe un temporaire. Avec `const std::wstring&`, la copie dans le membre serait systématique. Pour un paramètre seulement **lu**, `const T&` reste la règle (§2).

---

## 6. Erreurs : exceptions et codes de retour

Le C++ a des exceptions, mais **toutes sont « unchecked »** : aucune clause `throws`, le compilateur ne vérifie rien. On ne sait donc pas, en lisant une signature, si une fonction peut lever. Convention du projet : les fonctions qui peuvent échouer le disent dans leur commentaire (« Lève HrError en cas d'échec »).

**Les API Windows ne lèvent pas d'exceptions** : elles renvoient des codes (`HRESULT`, `BOOL` + `GetLastError`). Le projet mélange les deux mondes de façon contrôlée :

- à l'intérieur de notre code, `throwIfFailed(hr, "contexte")` convertit un échec en exception `HrError`, qui remonte jusqu'à un niveau capable de réagir ;
- **aux frontières avec le code système, on ne laisse JAMAIS sortir d'exception.** La procédure de fenêtre (`MainWindow::windowProc`) et les rappels COM (`CaptureSession::OnReadSample`) sont appelés par du code C qui ne sait pas les gérer : une exception qui le traverserait ferait planter le programme. D'où le `try/catch` « barrière » de `windowProc`, et l'absence de `throwIfFailed` dans les rappels de `CaptureSession` ;
- `wWinMain` contient la dernière barrière : toute exception non traitée y devient une boîte de dialogue lisible.

Pour une erreur **attendue**, comme une option mal saisie, on n'utilise pas d'exception : `parseArguments` renvoie un `ParseResult` qui contient soit les options, soit un message.

Détails de syntaxe :

- **attraper par référence constante** : `catch (const std::exception& e)`. Par valeur, l'exception serait copiée et tronquée à sa classe de base (*slicing*) ;
- `catch (...)` attrape tout, et `throw;` seul relance l'exception courante (cf. `CaptureSession::create`) ;
- `noexcept` sur une fonction promet qu'elle ne lève rien. Si elle le fait quand même, le programme s'arrête net.

---

## 7. Chaînes de caractères et encodage

En Java, une `String` est toujours de l'UTF-16. En C++, **`std::string` n'est qu'une suite d'octets** : l'encodage est une convention.

| Type | Caractère | Sous Windows | Utilisation |
| --- | --- | --- | --- |
| `std::string`, `"…"` | `char` (1 octet) | UTF-8 grâce à `/utf-8` | Messages d'exception (`what()`) |
| `std::wstring`, `L"…"` | `wchar_t` (2 octets) | UTF-16, comme Java | Tout le reste : les API « W » l'attendent |
| `std::wstring_view` | | | Vue en lecture seule, sans copie (paramètres de `log::info`) |

- `widen()` (`ComUtils.h`) convertit l'UTF-8 en UTF-16, par exemple pour afficher un message d'exception dans une boîte de dialogue.
- `.c_str()` donne un pointeur vers les caractères terminés par un zéro, la forme qu'attendent les API C.
- `==` compare le **contenu** des chaînes : pas besoin d'`equals`. (Sur deux pointeurs `const wchar_t*`, en revanche, `==` comparerait les adresses.)
- `std::format(L"{}x{}", w, h)` est l'équivalent de `String.format`, vérifié à la compilation.
- `substr(position, longueur)` prend une **longueur**, alors que `substring(début, fin)` de Java prend une fin. Piège classique (cf. `parseModeText`).
- `find` renvoie `std::wstring::npos` (et non -1) si rien n'est trouvé.
- `LR"(…)"` est un littéral brut, comme les *text blocks* `"""` de Java (cf. `helpText`).

---

## 8. Bibliothèque standard : conteneurs, `optional`, lambdas

**Conteneurs.** `std::vector<T>` est l'équivalent d'`ArrayList<T>`, avec deux différences importantes :

- il contient les **objets eux-mêmes**, pas des références. `push_back(mode)` copie `mode` dans le vecteur ;
- `v[i]` **ne vérifie pas les bornes** : un index hors limites est un comportement indéfini, sans `IndexOutOfBoundsException` (`v.at(i)` vérifie, mais on l'utilise rarement).

**Itérateurs.** Les algorithmes standard (`<algorithm>`) travaillent sur des paires d'**itérateurs** `[begin, end)`, l'équivalent d'un `Stream` sans la chaîne d'appels :

```cpp
// Selection.cpp : le max selon un comparateur
auto best = std::max_element(candidates.begin(), candidates.end(),
    [screen](const VideoMode& a, const VideoMode& b) {
        return preferenceKey(a, screen) < preferenceKey(b, screen);
    });
return *best;   // * : l'élément désigné par l'itérateur
```

| Java Stream | C++ `<algorithm>` |
| --- | --- |
| `anyMatch` | `std::any_of` |
| `max(comparator)` | `std::max_element` |
| `filter` + `collect` | boucle `for` + `push_back` (C++20 a aussi `std::ranges`) |

**`std::optional<T>`** correspond à `Optional<T>` : `std::nullopt` pour vide, test direct `if (mode)`, accès par `*mode` ou `mode->width`. Il contient la valeur elle-même, sans allocation.

**`std::tuple` et comparaison lexicographique.** `preferenceKey` renvoie un `std::tuple` ; l'opérateur `<` des tuples compare critère par critère. C'est l'équivalent compact d'un `Comparator.comparing(…).thenComparing(…)`.

**Lambdas : la capture est explicite.** En Java, un lambda capture implicitement les variables « effectivement finales ». En C++, on choisit entre crochets :

| Capture | Signification | Exemple du projet |
| --- | --- | --- |
| `[screen]` | Copie de `screen` | `pickBest` |
| `[&name]` | Référence à `name` | `scoreCaptureDevice` |
| `[&]` | Tout par référence (y compris modifiable) | `nextValue` dans `parseArguments` |
| `[this]` | Le pointeur de l'objet courant | Actions de fenêtre dans `App::App` |

**Danger** : un lambda qui capture par référence (ou `this`) ne doit pas survivre aux variables capturées. Sinon il référence un objet détruit (§13). Dans `App`, c'est garanti parce que la fenêtre qui stocke les lambdas est un membre d'`App`.

**`std::function<void(uint32_t, uint32_t)>`** peut contenir n'importe quel « appelable » avec cette signature (lambda, fonction…). C'est l'équivalent d'une interface fonctionnelle (`BiConsumer`). Cf. `MainWindow::onResize`.

---

## 9. Classes, héritage et interfaces

**`struct` et `class`** sont identiques, sauf la visibilité par défaut (publique pour `struct`, privée pour `class`). Convention du projet : `struct` pour un simple regroupement de données (`VideoMode`, `Options`), `class` pour un objet avec des invariants.

**Méthodes virtuelles.** En Java, toute méthode est redéfinissable. En C++, **seules les méthodes `virtual`** le sont ; les autres sont liées à la compilation. Une méthode `virtual … = 0` est **virtuelle pure** (abstraite). Une classe composée uniquement de méthodes virtuelles pures joue le rôle d'une interface Java : c'est exactement ce que sont les interfaces COM.

```cpp
class CaptureSession final : public IMFSourceReaderCallback {   // « implements »
    STDMETHODIMP OnReadSample(…) override;   // override : vérifié par le compilateur
};
```

- `: public Base` exprime l'héritage (toujours `public`, sauf cas rares) ;
- `override` correspond à `@Override`, mais il est vérifié par le compilateur ;
- `final` interdit l'héritage, comme en Java ;
- l'**héritage multiple** est autorisé (un objet COM implémente souvent plusieurs interfaces).

**Liste d'initialisation.** Les membres se construisent **avant** le corps du constructeur, dans une liste après `:` :

```cpp
App::App(HINSTANCE instance, const Options& options)
    : options_(options),
      window_(instance, !options.windowed),   // MainWindow n'a pas de constructeur par défaut :
      renderer_(std::make_unique<Renderer>(window_.handle())) { … }   // la liste est obligatoire
```

L'ordre effectif est celui de la **déclaration** des membres dans la classe, pas celui de la liste. C'est pourquoi `renderer_` peut utiliser `window_`, déclaré avant lui dans `App.h`.

**`static`** a le même sens qu'en Java dans une classe. Dans un `.cpp`, en revanche, on rend une fonction **privée au fichier** en la plaçant dans un `namespace { … }` anonyme (cf. `Selection.cpp`).

**Objets COM : constructeur privé + fabrique.** `CaptureSession` ne peut être créée que par `create()` et détruite que par `Release()`. Ce schéma impose le comptage de références COM ; il est détaillé dans [API_WINDOWS.md §2.4](API_WINDOWS.md#24-implémenter-un-objet-com--capturesession).

**Casts.** Le C++ distingue plusieurs conversions explicites, du plus sûr au plus dangereux :

| Cast | Usage | Exemple |
| --- | --- | --- |
| `static_cast<T>(x)` | Conversion « raisonnable » vérifiée à la compilation (entiers, enums, classe dérivée connue) | `static_cast<uint32_t>(rect.right - rect.left)` |
| `const_cast<T>(x)` | Retirer `const` pour une vieille API qui ne modifie pas l'objet | Rectangles de `VideoProcessorSet…Rect` |
| `reinterpret_cast<T>(x)` | Réinterpréter des bits : entier ↔ pointeur | `GWLP_USERDATA` dans `windowProc` |
| `dynamic_cast<T>(x)` | Cast vérifié à l'exécution (≈ `instanceof`) | Non utilisé ; en COM on passe par `QueryInterface` |

**Arguments par défaut et surcharge.** Java n'a pas de valeurs par défaut pour les paramètres ; on y écrit plusieurs méthodes surchargées. Le C++ permet les deux :

```cpp
// Arguments par défaut, déclarés une seule fois (dans le .h s'il y en a un)
ParseResult parseArguments(const std::vector<std::wstring>& args, const Options& base = {});
void addItem(HMENU menu, uint32_t id, const std::wstring& text, bool checked = false, bool enabled = true);

// Surcharge : même nom, types de paramètres différents
std::wstring toText(const RequestedMode& mode);
const wchar_t* toText(ScalingMode scaling);
```

Les valeurs par défaut se placent obligatoirement en fin de liste ; on ne peut pas « sauter » un paramètre du milieu. Elles sont écrites dans la **déclaration** (le `.h`), et non répétées dans la définition (le `.cpp`).

---

## 10. Fonctions de rappel (callbacks) C et pointeur this

Les API Windows prennent des **pointeurs de fonction** C : `windowClass.lpfnWndProc = &MainWindow::windowProc;`. Une méthode ordinaire ne convient pas, car elle a besoin d'un `this` que Windows ne fournit pas. On utilise donc une méthode **`static`** comme point d'entrée, qui retrouve l'objet puis appelle une vraie méthode :

```cpp
LRESULT CALLBACK MainWindow::windowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    MainWindow* self = reinterpret_cast<MainWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    …
    return self->handleMessage(message, wParam, lParam);
}
```

Le mécanisme complet (sauvegarde de `this` dans `WM_NCCREATE`) est décrit dans [API_WINDOWS.md §1.3](API_WINDOWS.md#13-relier-une-fenêtre-à-un-objet-c). En Java, une classe anonyme ou un lambda capture l'objet englobant automatiquement ; ici on le fait à la main.

`CALLBACK` et `STDMETHODIMP` fixent la **convention d'appel** (`__stdcall`), c'est-à-dire la façon dont les arguments sont passés en machine. Windows l'impose ; il suffit de reprendre exactement la signature documentée.

---

## 11. Threads et synchronisation

Le projet a deux sortes de threads :

- le **thread principal** (UI) : fenêtre, boucle de messages, rendu ;
- les **threads de Media Foundation**, qui appellent `CaptureSession::OnReadSample`.

En Java, chaque objet a un moniteur (`synchronized`). En C++, **rien n'est protégé par défaut**, ni les objets, ni les conteneurs standard. On déclare explicitement :

```cpp
std::mutex mutex_;                    // le verrou
ComPtr<IMFSample> latestSample_;      // protégé par mutex_ (le dire en commentaire)

ComPtr<IMFSample> CaptureSession::takeLatestSample() {
    std::lock_guard<std::mutex> lock(mutex_);   // verrouille ici…
    return std::move(latestSample_);
}                                               // …déverrouille ici (RAII)
```

- `std::lock_guard` est l'équivalent d'un bloc `synchronized` : verrouille à la construction, déverrouille à la destruction, même en cas d'exception. Pour limiter la durée du verrou, on ouvre un bloc `{ }` (cf. `CaptureSession::stop`).
- `std::atomic<T>` (`refCount_`, `error_`) : lecture/écriture atomiques sans verrou, comme `AtomicInteger`. Sans `atomic` ni mutex, un accès concurrent est un **comportement indéfini** (pas seulement une valeur « un peu périmée » comme avec un champ non `volatile` en Java).
- Pour **réveiller** le thread UI, on utilise un événement Windows (`SetEvent`) attendu par `MsgWaitForMultipleObjectsEx`. C'est l'équivalent d'un `notify()` qui s'intègre à la boucle de messages.

---

## 12. Types, espaces de noms et petites différences

**Entiers.** En Java, `int` fait toujours 32 bits. En C++, la taille de `int` et `long` dépend de la plateforme (`long` fait 32 bits sous Windows, 64 sous Linux). Pour une taille garantie : `int32_t`, `uint32_t`, `uint64_t` (`<cstdint>`).

**Entiers non signés.** Java n'en a (presque) pas ; le C++ en use beaucoup (`uint32_t`, `size_t` pour les tailles). Piège : `0u - 1` donne 4 294 967 295, pas -1. Et le mélange signé/non signé dans une comparaison donne des résultats surprenants. Le compilateur avertit (`/W4`) ; on convertit explicitement avec `static_cast`.

**Calculs qui débordent.** `3840 * 2160 * 2` tient en 32 bits, mais le produit croisé de `Geometry.h` (`dstW * srcH`) est fait en `int64_t` par précaution. En Java, un débordement donne une valeur fausse ; en C++, sur un entier signé, c'est un comportement indéfini (§13).

**Types Windows.** `DWORD` (32 bits non signé), `UINT`, `LONG`, `BOOL` (un `int` : `TRUE`/`FALSE`, à ne pas confondre avec le `bool` C++), `HRESULT`, `HANDLE`, `HWND`. Ce sont des alias (`typedef`) hérités du C.

**`auto`** déduit le type, comme `var`. À réserver aux cas où le type est évident ou très long (itérateurs, tuples).

**`enum class`** : énumération typée (`PixelFormat::Nv12`), sans conversion implicite en `int`. Contrairement à Java, elle ne peut avoir ni méthodes ni champs : on écrit des fonctions libres à côté (`toString(PixelFormat)`).

**`enum` « classique »** : ses valeurs se convertissent implicitement en entier. On l'utilise volontairement pour les identifiants de commande (`Commands.h`), que les API de menus Win32 manipulent comme des `UINT`. Pour ne pas polluer l'espace de noms, l'enum est placé dans un `namespace command` dédié : on écrit `command::Quit`.

**`switch`** ne fonctionne que sur des entiers et des enums, pas sur des chaînes comme en Java (depuis Java 7). Un `case` qui déclare une variable doit être placé entre accolades `{ }` (cf. `WM_KEYDOWN` dans `MainWindow.cpp`), sinon la portée de la variable déborde sur les `case` suivants et le compilateur refuse le code.

**`constexpr`** : valeur ou fonction calculable à la compilation (`kReconnectInterval`, `computeDestinationRect`). `static_assert` vérifie une condition pendant la compilation (cf. les tests).

**Espaces de noms.** `namespace hdmi { … }` regroupe le code comme un package, mais sans lien avec l'arborescence des dossiers. `using namespace hdmi;` équivaut à un `import hdmi.*` ; on l'évite dans les `.h`, car il s'imposerait à tous ceux qui les incluent.

**Templates.** `ComPtr<T>`, `std::vector<T>` sont des *templates*, l'équivalent des génériques. Différence de fond : ils sont instanciés à la compilation pour chaque type (pas d'effacement de type), d'où des messages d'erreur parfois très longs.

---

## 13. Comportements indéfinis : les pièges sans filet

Java protège contre la plupart des erreurs de mémoire par des exceptions (`NullPointerException`, `ArrayIndexOutOfBoundsException`). Le C++ ne vérifie rien à l'exécution : ces erreurs produisent un **comportement indéfini** (*undefined behavior*, UB). Le programme peut sembler fonctionner, planter plus loin, ou se comporter différemment en Release et en Debug.

| Piège | Exemple | Protection dans le projet |
| --- | --- | --- |
| Pointeur nul déréférencé | `sample->GetBufferByIndex(…)` avec `sample == nullptr` | Tests explicites (`if (!sample) return;`) |
| Objet utilisé après destruction (*dangling*) | Référence ou lambda vers une variable locale disparue | Ordre des membres, durées de vie documentées |
| Débordement de tableau | `v[i]` hors bornes, `memcpy` trop long | Calculs de taille vérifiés (`LockedBuffer`, contrôle du pitch) |
| Variable non initialisée | `int x; use(x);` | Initialisation systématique (`= 0`, `{}`) |
| Accès concurrent sans synchronisation | Deux threads sur `latestSample_` | `std::mutex`, `std::atomic` |
| Double libération | Deux copies d'un handle | Copie interdite (`= delete`), RAII |

Outils pour les détecter : compiler en **Debug** (Visual Studio active des vérifications sur les itérateurs) et activer **AddressSanitizer** (`/fsanitize=address`) le temps d'une session de test.

---

## 14. Build, configurations et débogage

- **Debug / Release.** Debug : pas d'optimisation, vérifications supplémentaires, débogage pas à pas fiable. Release : optimisé, beaucoup plus rapide, à utiliser pour l'usage réel. Les deux s'obtiennent avec le même `CMakeLists.txt` (`--config Debug` ou `--config Release`).
- **Runtime statique** (`/MT`, réglé dans `CMakeLists.txt`) : la bibliothèque C++ est intégrée au `.exe`, qui fonctionne sans installer de redistribuable Visual C++.
- **Avertissements** : `/W4` (MSVC) ou `-Wall -Wextra` (GCC). Le projet compile sans aucun avertissement ; c'est un bon réflexe à conserver, car beaucoup d'UB commencent par un avertissement ignoré.
- **Débogueur Visual Studio** : points d'arrêt, inspection des `ComPtr` (le pointeur interne s'appelle `ptr_`), fenêtre **Sortie** pour les messages de `log::info` (`OutputDebugStringW`).
- **Tests** : `ctest` lance `selection_tests`, qui couvre la logique pure (choix du mode, bandes noires, options). Le reste (capture, GPU) se teste en conditions réelles.
